//===-- PlatformDarwinKernel.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PlatformDarwinKernel.h"

#if defined (__APPLE__)  // This Plugin uses the Mac-specific source/Host/macosx/cfcpp utilities


// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/Error.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Host/FileSpec.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

#include <CoreFoundation/CoreFoundation.h>

#include "Host/macosx/cfcpp/CFCBundle.h"

using namespace lldb;
using namespace lldb_private;

//------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------
static uint32_t g_initialize_count = 0;

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------
void
PlatformDarwinKernel::Initialize ()
{
    if (g_initialize_count++ == 0)
    {
        PluginManager::RegisterPlugin (PlatformDarwinKernel::GetShortPluginNameStatic(),
                                       PlatformDarwinKernel::GetDescriptionStatic(),
                                       PlatformDarwinKernel::CreateInstance);
    }
}

void
PlatformDarwinKernel::Terminate ()
{
    if (g_initialize_count > 0)
    {
        if (--g_initialize_count == 0)
        {
            PluginManager::UnregisterPlugin (PlatformDarwinKernel::CreateInstance);
        }
    }
}

Platform*
PlatformDarwinKernel::CreateInstance (bool force, const ArchSpec *arch)
{
    // This is a special plugin that we don't want to activate just based on an ArchSpec for normal
    // userlnad debugging.  It is only useful in kernel debug sessions and the DynamicLoaderDarwinPlugin
    // (or a user doing 'platform select') will force the creation of this Platform plugin.
    if (force == false)
        return NULL;

    bool create = force;
    LazyBool is_ios_debug_session = eLazyBoolCalculate;

    if (create == false && arch && arch->IsValid())
    {
        const llvm::Triple &triple = arch->GetTriple();
        switch (triple.getVendor())
        {
            case llvm::Triple::Apple:
                create = true;
                break;
                
            // Only accept "unknown" for vendor if the host is Apple and
            // it "unknown" wasn't specified (it was just returned becasue it
            // was NOT specified)
            case llvm::Triple::UnknownArch:
                create = !arch->TripleVendorWasSpecified();
                break;
            default:
                break;
        }
        
        if (create)
        {
            switch (triple.getOS())
            {
                case llvm::Triple::Darwin:  // Deprecated, but still support Darwin for historical reasons
                case llvm::Triple::MacOSX:
                    break;
                // Only accept "vendor" for vendor if the host is Apple and
                // it "unknown" wasn't specified (it was just returned becasue it
                // was NOT specified)
                case llvm::Triple::UnknownOS:
                    create = !arch->TripleOSWasSpecified();
                    break;
                default:
                    create = false;
                    break;
            }
        }
    }
    if (arch && arch->IsValid())
    {
        switch (arch->GetMachine())
        {
        case llvm::Triple::x86:
        case llvm::Triple::x86_64:
        case llvm::Triple::ppc:
        case llvm::Triple::ppc64:
            is_ios_debug_session = eLazyBoolNo;
            break;
        case llvm::Triple::arm:
        case llvm::Triple::thumb:
            is_ios_debug_session = eLazyBoolYes;
            break;
        default:
            is_ios_debug_session = eLazyBoolCalculate;
            break;
        }
    }
    if (create)
        return new PlatformDarwinKernel (is_ios_debug_session);
    return NULL;
}


const char *
PlatformDarwinKernel::GetPluginNameStatic ()
{
    return "PlatformDarwinKernel";
}

const char *
PlatformDarwinKernel::GetShortPluginNameStatic()
{
    return "darwin-kernel";
}

const char *
PlatformDarwinKernel::GetDescriptionStatic()
{
    return "Darwin Kernel platform plug-in.";
}


//------------------------------------------------------------------
/// Default Constructor
//------------------------------------------------------------------
PlatformDarwinKernel::PlatformDarwinKernel (lldb_private::LazyBool is_ios_debug_session) :
    PlatformDarwin (false),    // This is a remote platform
    m_name_to_kext_path_map(),
    m_directories_searched(),
    m_ios_debug_session(is_ios_debug_session)

{
    SearchForKexts ();
}

//------------------------------------------------------------------
/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
//------------------------------------------------------------------
PlatformDarwinKernel::~PlatformDarwinKernel()
{
}


void
PlatformDarwinKernel::GetStatus (Stream &strm)
{
    Platform::GetStatus (strm);
    strm.Printf (" Debug session type: ");
    if (m_ios_debug_session == eLazyBoolYes)
        strm.Printf ("iOS kernel debugging\n");
    else if (m_ios_debug_session == eLazyBoolNo)
        strm.Printf ("Mac OS X kernel debugging\n");
    else
            strm.Printf ("unknown kernel debugging\n");
    const uint32_t num_kdk_dirs = m_directories_searched.size();
    for (uint32_t i=0; i<num_kdk_dirs; ++i)
    {
        const FileSpec &kdk_dir = m_directories_searched[i];

        strm.Printf (" KDK Roots: [%2u] \"%s/%s\"\n",
                     i,
                     kdk_dir.GetDirectory().GetCString(),
                     kdk_dir.GetFilename().GetCString());
    }
    strm.Printf (" Total number of kexts indexed: %d\n", (int) m_name_to_kext_path_map.size());
}

void
PlatformDarwinKernel::SearchForKexts ()
{
    // Differentiate between "ios debug session" and "mac debug session" so we don't index
    // kext bundles that won't be used in this debug session.  If this is an ios kext debug
    // session, looking in /System/Library/Extensions is a waste of stat()s, for example.

    // Build up a list of all SDKs we'll be searching for directories of kexts
    // e.g. /Applications/Xcode.app//Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.8.Internal.sdk
    std::vector<FileSpec> sdk_dirs;
    if (m_ios_debug_session != eLazyBoolNo)
        GetiOSSDKDirectoriesToSearch (sdk_dirs);
    if (m_ios_debug_session != eLazyBoolYes)
        GetMacSDKDirectoriesToSearch (sdk_dirs);

    GetGenericSDKDirectoriesToSearch (sdk_dirs);

    // Build up a list of directories that hold kext bundles on the system
    // e.g. /Applications/Xcode.app//Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.8.Internal.sdk/System/Library/Extensions
    std::vector<FileSpec> kext_dirs;
    SearchSDKsForKextDirectories (sdk_dirs, kext_dirs);

    if (m_ios_debug_session != eLazyBoolNo)
        GetiOSDirectoriesToSearch (kext_dirs);
    if (m_ios_debug_session != eLazyBoolYes)
        GetMacDirectoriesToSearch (kext_dirs);

    GetGenericDirectoriesToSearch (kext_dirs);

    // We now have a complete list of directories that we will search for kext bundles
    m_directories_searched = kext_dirs;

    IndexKextsInDirectories (kext_dirs);
}

void
PlatformDarwinKernel::GetiOSSDKDirectoriesToSearch (std::vector<lldb_private::FileSpec> &directories)
{
    // DeveloperDirectory is something like "/Applications/Xcode.app/Contents/Developer"
    const char *developer_dir = GetDeveloperDirectory();
    if (developer_dir == NULL)
        developer_dir = "/Applications/Xcode.app/Contents/Developer";

    char pathbuf[PATH_MAX];
    ::snprintf (pathbuf, sizeof (pathbuf), "%s/Platforms/iPhoneOS.platform/Developer/SDKs", developer_dir);
    FileSpec ios_sdk(pathbuf, true);
    if (ios_sdk.Exists() && ios_sdk.GetFileType() == FileSpec::eFileTypeDirectory)
    {
        directories.push_back (ios_sdk);
    }
}

void
PlatformDarwinKernel::GetMacSDKDirectoriesToSearch (std::vector<lldb_private::FileSpec> &directories)
{
    // DeveloperDirectory is something like "/Applications/Xcode.app/Contents/Developer"
    const char *developer_dir = GetDeveloperDirectory();
    if (developer_dir == NULL)
        developer_dir = "/Applications/Xcode.app/Contents/Developer";

    char pathbuf[PATH_MAX];
    ::snprintf (pathbuf, sizeof (pathbuf), "%s/Platforms/MacOSX.platform/Developer/SDKs", developer_dir);
    FileSpec mac_sdk(pathbuf, true);
    if (mac_sdk.Exists() && mac_sdk.GetFileType() == FileSpec::eFileTypeDirectory)
    {
        directories.push_back (mac_sdk);
    }
}

void
PlatformDarwinKernel::GetGenericSDKDirectoriesToSearch (std::vector<lldb_private::FileSpec> &directories)
{
    FileSpec generic_sdk("/AppleInternal/Developer/KDKs", true);
    if (generic_sdk.Exists() && generic_sdk.GetFileType() == FileSpec::eFileTypeDirectory)
    {
        directories.push_back (generic_sdk);
    }
}

void
PlatformDarwinKernel::GetiOSDirectoriesToSearch (std::vector<lldb_private::FileSpec> &directories)
{
}

void
PlatformDarwinKernel::GetMacDirectoriesToSearch (std::vector<lldb_private::FileSpec> &directories)
{
    FileSpec sle("/System/Library/Extensions", true);
    if (sle.Exists() && sle.GetFileType() == FileSpec::eFileTypeDirectory)
    {
        directories.push_back(sle);
    }
}

void
PlatformDarwinKernel::GetGenericDirectoriesToSearch (std::vector<lldb_private::FileSpec> &directories)
{
    // DeveloperDirectory is something like "/Applications/Xcode.app/Contents/Developer"
    const char *developer_dir = GetDeveloperDirectory();
    if (developer_dir == NULL)
        developer_dir = "/Applications/Xcode.app/Contents/Developer";

    char pathbuf[PATH_MAX];
    ::snprintf (pathbuf, sizeof (pathbuf), "%s/../Symbols", developer_dir);
    FileSpec symbols_dir (pathbuf, true);
    if (symbols_dir.Exists() && symbols_dir.GetFileType() == FileSpec::eFileTypeDirectory)
    {
        directories.push_back (symbols_dir);
    }
}

// Scan through the SDK directories, looking for directories where kexts are likely.
// Add those directories to kext_dirs.
void
PlatformDarwinKernel::SearchSDKsForKextDirectories (std::vector<lldb_private::FileSpec> sdk_dirs, std::vector<lldb_private::FileSpec> &kext_dirs)
{
    const uint32_t num_sdks = sdk_dirs.size();
    for (uint32_t i = 0; i < num_sdks; i++)
    {
        const FileSpec &sdk_dir = sdk_dirs[i];
        char pathbuf[PATH_MAX];
        if (sdk_dir.GetPath (pathbuf, sizeof (pathbuf)))
        {
            const bool find_directories = true;
            const bool find_files = false;
            const bool find_other = false;
            FileSpec::EnumerateDirectory (pathbuf,
                                          find_directories,
                                          find_files,
                                          find_other,
                                          GetKextDirectoriesInSDK,
                                          &kext_dirs);
        }
    }
}

// Callback for FileSpec::EnumerateDirectory().  
// Step through the entries in a directory like
//    /Applications/Xcode.app//Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs
// looking for any subdirectories of the form MacOSX10.8.Internal.sdk/System/Library/Extensions
// Adds these to the vector of FileSpec's.

FileSpec::EnumerateDirectoryResult
PlatformDarwinKernel::GetKextDirectoriesInSDK (void *baton,
                                               FileSpec::FileType file_type,
                                               const FileSpec &file_spec)
{
    if (file_type == FileSpec::eFileTypeDirectory 
        && (file_spec.GetFileNameExtension() == ConstString("sdk")
            || file_spec.GetFileNameExtension() == ConstString("kdk")))
    {
        char pathbuf[PATH_MAX];
        if (file_spec.GetPath (pathbuf, PATH_MAX))
        {
            char kext_directory_str[PATH_MAX];
            ::snprintf (kext_directory_str, sizeof (kext_directory_str), "%s/%s", pathbuf, "System/Library/Extensions");
            FileSpec kext_directory (kext_directory_str, true);
            if (kext_directory.Exists() && kext_directory.GetFileType() == FileSpec::eFileTypeDirectory)
            {
                ((std::vector<lldb_private::FileSpec> *)baton)->push_back(kext_directory);
            }
        }
    }
    return FileSpec::eEnumerateDirectoryResultNext;
}

void
PlatformDarwinKernel::IndexKextsInDirectories (std::vector<lldb_private::FileSpec> kext_dirs)
{
    std::vector<FileSpec> kext_bundles;
    const uint32_t num_dirs = kext_dirs.size();
    for (uint32_t i = 0; i < num_dirs; i++)
    {
        const FileSpec &dir = kext_dirs[i];
        char pathbuf[PATH_MAX];
        if (dir.GetPath (pathbuf, sizeof(pathbuf)))
        {
            const bool find_directories = true;
            const bool find_files = false;
            const bool find_other = false;
            FileSpec::EnumerateDirectory (pathbuf,
                                          find_directories,
                                          find_files,
                                          find_other,
                                          GetKextsInDirectory,
                                          &kext_bundles);
        }
    }

    const uint32_t num_kexts = kext_bundles.size();
    for (uint32_t i = 0; i < num_kexts; i++)
    {
        const FileSpec &kext = kext_bundles[i];
        char pathbuf[PATH_MAX];
        if (kext.GetPath (pathbuf, sizeof (pathbuf)))
        {
            CFCBundle bundle (pathbuf);
            CFStringRef bundle_id (bundle.GetIdentifier());
            if (bundle_id && CFGetTypeID (bundle_id) == CFStringGetTypeID ())
            {
                char bundle_id_buf[PATH_MAX];
                if (CFStringGetCString (bundle_id, bundle_id_buf, sizeof (bundle_id_buf), kCFStringEncodingUTF8))
                {
                    ConstString bundle_conststr(bundle_id_buf);
                    m_name_to_kext_path_map.insert(std::pair<ConstString, FileSpec>(bundle_conststr, kext));
                }
            }
        }
    }
}

// Callback for FileSpec::EnumerateDirectory().
// Step through the entries in a directory like /System/Library/Extensions, find .kext bundles, add them
// to the vector of FileSpecs.
// If a .kext bundle has a Contents/PlugIns or PlugIns subdir, search for kexts in there too.

FileSpec::EnumerateDirectoryResult
PlatformDarwinKernel::GetKextsInDirectory (void *baton,
                                           FileSpec::FileType file_type,
                                           const FileSpec &file_spec)
{
    if (file_type == FileSpec::eFileTypeDirectory && file_spec.GetFileNameExtension() == ConstString("kext"))
    {
        ((std::vector<lldb_private::FileSpec> *)baton)->push_back(file_spec);
        bool search_inside = false;
        char pathbuf[PATH_MAX];
        ::snprintf (pathbuf, sizeof (pathbuf), "%s/%s/Contents/PlugIns", file_spec.GetDirectory().GetCString(), file_spec.GetFilename().GetCString());
        FileSpec contents_plugins (pathbuf, false);
        if (contents_plugins.Exists() && contents_plugins.GetFileType() == FileSpec::eFileTypeDirectory)
        {
            search_inside = true;
        }
        else
        {
            ::snprintf (pathbuf, sizeof (pathbuf), "%s/%s/PlugIns", file_spec.GetDirectory().GetCString(), file_spec.GetFilename().GetCString());
            FileSpec plugins (pathbuf, false);
            if (plugins.Exists() && plugins.GetFileType() == FileSpec::eFileTypeDirectory)
            {
                search_inside = true;
            }
        }

        if (search_inside)
        {
            const bool find_directories = true;
            const bool find_files = false;
            const bool find_other = false;
            FileSpec::EnumerateDirectory (pathbuf,
                                          find_directories,
                                          find_files,
                                          find_other,
                                          GetKextsInDirectory,
                                          baton);
        }
    }
    return FileSpec::eEnumerateDirectoryResultNext;
}

Error
PlatformDarwinKernel::GetSharedModule (const ModuleSpec &module_spec,
                                       ModuleSP &module_sp,
                                       const FileSpecList *module_search_paths_ptr,
                                       ModuleSP *old_module_sp_ptr,
                                       bool *did_create_ptr)
{
    Error error;
    module_sp.reset();
    const FileSpec &platform_file = module_spec.GetFileSpec();
    char kext_bundle_id[PATH_MAX];
    if (platform_file.GetPath (kext_bundle_id, sizeof (kext_bundle_id)))
    {
        ConstString kext_bundle_cs(kext_bundle_id);
        if (m_name_to_kext_path_map.count(kext_bundle_cs) > 0)
        {
            for (BundleIDToKextIterator it = m_name_to_kext_path_map.begin (); it != m_name_to_kext_path_map.end (); ++it)
            {
                if (it->first == kext_bundle_cs)
                {
                    error = ExamineKextForMatchingUUID (it->second, module_spec.GetUUID(), module_sp);
                    if (module_sp.get())
                    {
                        return error;
                    }
                }
            }
        }
    }

    return error;
}

Error
PlatformDarwinKernel::ExamineKextForMatchingUUID (const FileSpec &kext_bundle_path, const lldb_private::UUID &uuid, ModuleSP &exe_module_sp)
{
    Error error;
    FileSpec exe_file = kext_bundle_path;
    Host::ResolveExecutableInBundle (exe_file);
    if (exe_file.Exists())
    {
        ModuleSpec exe_spec (exe_file);
        exe_spec.GetUUID() = uuid;
        error = ModuleList::GetSharedModule (exe_spec, exe_module_sp, NULL, NULL, NULL);
        if (exe_module_sp && exe_module_sp->GetObjectFile())
        {
            return error;
        }
        exe_module_sp.reset();
    }
    return error;
}

bool
PlatformDarwinKernel::GetSupportedArchitectureAtIndex (uint32_t idx, ArchSpec &arch)
{
#if defined (__arm__)
    return ARMGetSupportedArchitectureAtIndex (idx, arch);
#else
    return x86GetSupportedArchitectureAtIndex (idx, arch);
#endif
}

#endif // __APPLE__