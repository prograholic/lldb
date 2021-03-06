//===-- MICmnMIValueConst.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmnMIValue.h"

//++ ============================================================================
// Details: MI common code MI Result class. Part of the CMICmnMIValueConstRecord
//          set of objects.
//          The syntax is as follows:
//          result-record ==>  [ token ] "^" result-class ( "," result )* nl
//          token = any sequence of digits
//          * = 0 to many
//          nl = CR | CR_LF
//          result-class ==> "done" | "running" | "connected" | "error" | "exit"
//          result ==> variable "=" value
//          value ==> const | tuple | list
//          const ==> c-string (7 bit iso c string content)
//          tuple ==>  "{}" | "{" result ( "," result )* "}"
//          list ==>  "[]" | "[" value ( "," value )* "]" | "[" result ( "," result )* "]"
//          More information see:
//          http://ftp.gnu.org/old-gnu/Manuals/gdb-5.1.1/html_chapter/gdb_22.html
//
//          The text formed in *this Result class is stripped of any '\n' characters.
//
// Gotchas: None.
// Authors: Illya Rudkin 24/02/2014.
// Changes: None.
//--
class CMICmnMIValueConst : public CMICmnMIValue
{
    // Methods:
  public:
    /* ctor */ CMICmnMIValueConst(const CMIUtilString &vString);
    /* ctor */ CMICmnMIValueConst(const CMIUtilString &vString, const bool vbNoQuotes);

    // Overridden:
  public:
    // From CMICmnBase
    /* dtor */ virtual ~CMICmnMIValueConst(void);

    // Methods:
  private:
    bool BuildConst(void);

    // Attributes:
  private:
    static const CMIUtilString ms_constStrDblQuote;
    //
    CMIUtilString m_strPartConst;
    bool m_bNoQuotes; // True = return string not surrounded with quotes, false = use quotes
};
