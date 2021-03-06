//===-- MICmnLLDBDebuggerHandleEvents.h -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmnBase.h"
#include "MICmnMIValueList.h"
#include "MICmnMIValueTuple.h"
#include "MIUtilSingletonBase.h"

// Declarations:
class CMICmnLLDBDebugSessionInfo;
class CMICmnMIResultRecord;
class CMICmnStreamStdout;
class CMICmnMIOutOfBandRecord;

//++ ============================================================================
// Details: MI class to take LLDB SBEvent objects, filter them and form
//          MI Out-of-band records from the information inside the event object.
//          These records are then pushed to stdout.
//          A singleton class.
// Gotchas: None.
// Authors: Illya Rudkin 02/03/2014.
// Changes: None.
//--
class CMICmnLLDBDebuggerHandleEvents : public CMICmnBase, public MI::ISingleton<CMICmnLLDBDebuggerHandleEvents>
{
    friend class MI::ISingleton<CMICmnLLDBDebuggerHandleEvents>;

    // Methods:
  public:
    bool Initialize(void);
    bool Shutdown(void);
    //
    bool HandleEvent(const lldb::SBEvent &vEvent, bool &vrbHandledEvent);

    // Methods:
  private:
    /* ctor */ CMICmnLLDBDebuggerHandleEvents(void);
    /* ctor */ CMICmnLLDBDebuggerHandleEvents(const CMICmnLLDBDebuggerHandleEvents &);
    void operator=(const CMICmnLLDBDebuggerHandleEvents &);
    //
    bool ChkForStateChanges(void);
    bool GetProcessStdout(void);
    bool GetProcessStderr(void);
    bool HandleEventSBBreakPoint(const lldb::SBEvent &vEvent);
    bool HandleEventSBBreakpointCmn(const lldb::SBEvent &vEvent);
    bool HandleEventSBBreakpointAdded(const lldb::SBEvent &vEvent);
    bool HandleEventSBBreakpointLocationsAdded(const lldb::SBEvent &vEvent);
    bool HandleEventSBProcess(const lldb::SBEvent &vEvent);
    bool HandleEventSBTarget(const lldb::SBEvent &vEvent);
    bool HandleEventSBThread(const lldb::SBEvent &vEvent);
    bool HandleEventSBThreadBitStackChanged(const lldb::SBEvent &vEvent);
    bool HandleEventSBThreadSuspended(const lldb::SBEvent &vEvent);
    bool HandleEventSBCommandInterpreter(const lldb::SBEvent &vEvent);
    bool HandleProcessEventBroadcastBitStateChanged(const lldb::SBEvent &vEvent);
    bool HandleProcessEventStateRunning(void);
    bool HandleProcessEventStateExited(void);
    bool HandleProcessEventStateStopped(bool &vwrbShouldBrk);
    bool HandleProcessEventStopReasonTrace(void);
    bool HandleProcessEventStopReasonBreakpoint(void);
    bool HandleProcessEventStopSignal(bool &vwrbShouldBrk);
    bool HandleProcessEventStopException(void);
    bool HandleProcessEventStateSuspended(const lldb::SBEvent &vEvent);
    bool HandleTargetEventBroadcastBitModulesLoaded(const lldb::SBEvent &vEvent);
    bool HandleTargetEventBroadcastBitModulesUnloaded(const lldb::SBEvent &vEvent);
    bool MiHelpGetModuleInfo(const lldb::SBModule &vModule, const MIuint nModuleNum,
                             CMICmnMIValueList &vwrMiValueList);
    bool MiHelpGetCurrentThreadFrame(CMICmnMIValueTuple &vwrMiValueTuple);
    bool MiResultRecordToStdout(const CMICmnMIResultRecord &vrMiResultRecord);
    bool MiOutOfBandRecordToStdout(const CMICmnMIOutOfBandRecord &vrMiResultRecord);
    bool MiStoppedAtBreakPoint(const MIuint64 vBrkPtId, const lldb::SBBreakpoint &vBrkPt);
    bool TextToStdout(const CMIUtilString &vrTxt);
    bool TextToStderr(const CMIUtilString &vrTxt);
    bool UpdateSelectedThread(void);

    // Overridden:
  private:
    // From CMICmnBase
    /* dtor */ virtual ~CMICmnLLDBDebuggerHandleEvents(void);
    void InitializeSignals();
    bool m_bSignalsInitialized;
    MIuint64 m_SIGINT;
    MIuint64 m_SIGSTOP;
    MIuint64 m_SIGSEGV;
    MIuint64 m_SIGTRAP;
};
