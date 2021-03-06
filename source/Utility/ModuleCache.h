//===-- ModuleCache.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_ModuleCache_h_
#define utility_ModuleCache_h_

#include "lldb/lldb-types.h"
#include "lldb/lldb-forward.h"

#include "lldb/Core/Error.h"
#include "lldb/Host/FileSpec.h"

#include <string>
#include <unordered_map>

namespace lldb_private {

class Module;
class UUID;

//----------------------------------------------------------------------
/// @class ModuleCache ModuleCache.h "Utility/ModuleCache.h"
/// @brief A module cache class.
///
/// Caches locally modules that are downloaded from remote targets.
/// Each cached module maintains 2 views:
///  - UUID view:    /${CACHE_ROOT}/${PLATFORM_NAME}/.cache/${UUID}/${MODULE_FILENAME}
///  - Sysroot view: /${CACHE_ROOT}/${PLATFORM_NAME}/${HOSTNAME}/${MODULE_FULL_FILEPATH}
///
/// UUID views stores a real module file, whereas Sysroot view holds a symbolic
/// link to UUID-view file.
///
/// Example:
/// UUID view   : /tmp/lldb/remote-linux/.cache/30C94DC6-6A1F-E951-80C3-D68D2B89E576-D5AE213C/libc.so.6
/// Sysroot view: /tmp/lldb/remote-linux/ubuntu/lib/x86_64-linux-gnu/libc.so.6
//----------------------------------------------------------------------

class ModuleCache
{
public:
    Error
    Put (const FileSpec &root_dir_spec,
         const char *hostname,
         const ModuleSpec &module_spec,
         const FileSpec &tmp_file);

    Error
    Get (const FileSpec &root_dir_spec,
         const char *hostname,
         const ModuleSpec &module_spec,
         lldb::ModuleSP &cached_module_sp);

private:
    static FileSpec
    GetModuleDirectory (const FileSpec &root_dir_spec, const UUID &uuid);

    static FileSpec
    GetHostSysRootModulePath (const FileSpec &root_dir_spec, const char *hostname, const FileSpec &platform_module_spec);

    static Error
    CreateHostSysRootModuleSymLink (const FileSpec &sysroot_module_path_spec, const FileSpec &module_file_path);

    std::unordered_map<std::string, lldb::ModuleWP> m_loaded_modules;
};

} // namespace lldb_private

#endif  // utility_ModuleCache_h_
