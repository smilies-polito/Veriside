// -*- mode: C++; c-file-style: "cc-mode" -*-
//=============================================================================
//
// Code available from: https://verilator.org
//
// Copyright 2001-2023 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//=============================================================================
///
/// \file
/// \brief Verilated C++ tracing in Side format implementation code
///
/// This file must be compiled and linked against all Verilated objects
/// that use --trace.
///
/// Use "verilator --trace" to add this to the Makefile for the linker.
///
//=============================================================================

// clang-format off

#include "verilatedos.h"
#include "verilated.h"
#include "verilated_side_c.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <cerrno>
#include <fcntl.h>

#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
# include <io.h>
#else
# include <unistd.h>
#endif

#ifndef O_LARGEFILE  // WIN32 headers omit this
# define O_LARGEFILE 0
#endif
#ifndef O_NONBLOCK  // WIN32 headers omit this
# define O_NONBLOCK 0
#endif
#ifndef O_CLOEXEC  // WIN32 headers omit this
# define O_CLOEXEC 0
#endif

// clang-format on

// This size comes form VCD allowing use of printable ASCII characters between
// '!' and '~' inclusive, which are a total of 94 different values. Encoding a
// 32 bit code hence needs a maximum of std::ceil(log94(2**32-1)) == 5 bytes.
constexpr unsigned VL_TRACE_MAX_VCD_CODE_SIZE = 5;  // Maximum length of a VCD string code

// We use 8 bytes per code in a suffix buffer array.
// 1 byte optional separator + VL_TRACE_MAX_VCD_CODE_SIZE bytes for code
// + 1 byte '\n' + 1 byte suffix size. This luckily comes out to a power of 2,
// meaning the array can be aligned such that entries never straddle multiple
// cache-lines.
constexpr unsigned VL_TRACE_SUFFIX_ENTRY_SIZE = 8;  // Size of a suffix entry

//=============================================================================
// Utility functions: TODO: put these in a common place and share them.

template <size_t N>
static size_t roundUpToMultipleOf(size_t value) {
    static_assert((N & (N - 1)) == 0, "'N' must be a power of 2");
    size_t mask = N - 1;
    return (value + mask) & ~mask;
}

//=============================================================================
// Specialization of the generics for this trace format

#define VL_SUB_T VerilatedSide
#define VL_BUF_T VerilatedSideBuffer
#include "verilated_trace_imp.h"
#undef VL_SUB_T
#undef VL_BUF_T

// SideTrace namespace implementation

namespace SideTrace {
    // Dynamic configuration variables
    int kNumInstances = 0;
    std::vector<std::ofstream> output_files;
    std::vector<std::string> instance_names;
    std::vector<std::unordered_map<uint32_t, std::string>> filtered_signals;
    std::vector<std::uint32_t> switching_activity;
    std::string trigger_signal_name;
    
    // Atomic variables for trace thread synchronization
    std::atomic<bool> trigger_data_flag{false};
    std::atomic<bool> prev_trigger_flag{false};
    std::atomic<bool> monitor_enabled{false};
    std::uint32_t trigger_data_code = 0;
    std::uint32_t time_window = 0;
    
    // Hamming distance vs hamming weight flag
    bool use_hamming_weight = false;
    
    // Validation tracking
    bool trigger_signal_found = false;
    std::vector<bool> modules_found;

    void configure(const std::vector<std::string>& modules, const std::string& trigger, bool hammingWeight) {
        kNumInstances = modules.size();
        instance_names = modules;
        trigger_signal_name = trigger;
        use_hamming_weight = hammingWeight;

        // Resize all vectors
        output_files.resize(kNumInstances);
        filtered_signals.resize(kNumInstances);
        switching_activity.resize(kNumInstances);
        
        // Initialize tracking vectors
        modules_found.resize(kNumInstances, false);
        trigger_signal_found = false;
        
        // Initialize switching activity to 0
        std::fill(switching_activity.begin(), switching_activity.end(), 0);
        
        // Enable monitoring when configuration is set
        monitor_enabled.store(true);
    }
    
    void validateConfiguration(VerilatedSideFile* filep) {
        // Check if trigger signal was found
        if (!trigger_signal_name.empty() && !trigger_signal_found) {
            std::cerr << "ERROR: Trigger signal '" << trigger_signal_name << "' not found in any traced signals!" << std::endl;
            std::cerr << "Make sure the trigger signal name matches exactly with a signal in your design." << std::endl;
            exit(1);
        }
        
        // Check if all required modules were found
        for (size_t i = 0; i < instance_names.size(); ++i) {
            if (!modules_found[i]) {
                std::cerr << "ERROR: Module '" << instance_names[i] << "' not found in any traced signals!" << std::endl;
                std::cerr << "Make sure the module name matches exactly with a module in your design." << std::endl;
                exit(1);
            }
        }
        
        // Validation passed - no file writing here anymore
    }
    
    std::string generateValidationOutput() {
        std::string validation_output = "\n\nSide channel configuration validated successfully:\n";
        validation_output += "  - Trigger signal: " + trigger_signal_name + " ✓\n";
        
        // Print all filtered signals for each module
        for (size_t i = 0; i < instance_names.size(); ++i) {
            validation_output += "  - Module: " + instance_names[i] + " ✓ (" + 
                                std::to_string(filtered_signals[i].size()) + " signals)\n";
            for (const auto& signal_pair : filtered_signals[i]) {
                validation_output += "    * " + signal_pair.second + "\n";
            }
        }
        
        return validation_output;
    }
}

//=============================================================================
// VerilatedSideFile

bool VerilatedSideFile::open(const std::string& name) VL_MT_UNSAFE {
    m_fd = ::open(name.c_str(),
                  O_CREAT | O_WRONLY | O_TRUNC | O_LARGEFILE | O_NONBLOCK | O_CLOEXEC, 0666);
    return m_fd >= 0;
}

void VerilatedSideFile::close() VL_MT_UNSAFE { ::close(m_fd); }

ssize_t VerilatedSideFile::write(const char* bufp, ssize_t len) VL_MT_UNSAFE {
    return ::write(m_fd, bufp, len);
}

//=============================================================================
//=============================================================================
//=============================================================================
// Opening/Closing

VerilatedSide::VerilatedSide(VerilatedSideFile* filep) {
    // Not in header to avoid link issue if header is included without this .cpp file
    m_fileNewed = (filep == nullptr);
    m_filep = m_fileNewed ? new VerilatedSideFile : filep;
    m_wrChunkSize = 8 * 1024;
    m_wrBufp = new char[m_wrChunkSize * 8];
    m_wrFlushp = m_wrBufp + m_wrChunkSize * 6;
    m_writep = m_wrBufp;
}

void VerilatedSide::open(const char* filename) VL_MT_SAFE_EXCLUDES(m_mutex) {

    const VerilatedLockGuard lock{m_mutex};
    if (isOpen()) return;

    // Set member variables
    m_filename = filename;  // "" is ok, as someone may overload open

    openNextImp(m_rolloverSize != 0);
    if (!isOpen()) return;

    dumpHeader();

    // When using rollover, the first chunk contains the header only.
    if (m_rolloverSize) openNextImp(true);

    // Open the global file stream
    for (int i = 0; i < SideTrace::kNumInstances; ++i) {
        SideTrace::output_files[i].open(SideTrace::instance_names[i] + ".side", std::ios::out);
        SideTrace::output_files[i] << "{\n";
    }
}

void VerilatedSide::openNext(bool incFilename) VL_MT_SAFE_EXCLUDES(m_mutex) {
    // Open next filename in concat sequence, mangle filename if
    // incFilename is true.
    const VerilatedLockGuard lock{m_mutex};
    openNextImp(incFilename);
}

void VerilatedSide::openNextImp(bool incFilename) {
    closePrev();  // Close existing
    if (incFilename) {
        // Find _0000.{ext} in filename
        std::string name = m_filename;
        const size_t pos = name.rfind('.');
        if (pos > 8 && 0 == std::strncmp("_cat", name.c_str() + pos - 8, 4)
            && std::isdigit(name.c_str()[pos - 4]) && std::isdigit(name.c_str()[pos - 3])
            && std::isdigit(name.c_str()[pos - 2]) && std::isdigit(name.c_str()[pos - 1])) {
            // Increment code.
            if ((++(name[pos - 1])) > '9') {
                name[pos - 1] = '0';
                if ((++(name[pos - 2])) > '9') {
                    name[pos - 2] = '0';
                    if ((++(name[pos - 3])) > '9') {
                        name[pos - 3] = '0';
                        if ((++(name[pos - 4])) > '9') {  //
                            name[pos - 4] = '0';
                        }
                    }
                }
            }
        } else {
            // Append _cat0000
            name.insert(pos, "_cat0000");
        }
        m_filename = name;
    }
    if (VL_UNCOVERABLE(m_filename[0] == '|')) {
        assert(0);  // LCOV_EXCL_LINE // Not supported yet.
    } else {
        // cppcheck-suppress duplicateExpression
        if (!m_filep->open(m_filename)) {
            // User code can check isOpen()
            m_isOpen = false;
            return;
        }
    }
    m_isOpen = true;
    fullDump(true);  // First dump must be full
    m_wroteBytes = 0;
}

bool VerilatedSide::preChangeDump() {
    if (VL_UNLIKELY(m_rolloverSize && m_wroteBytes > m_rolloverSize)) openNextImp(true);
    return isOpen();
}

void VerilatedSide::emitTimeChange(uint64_t timeui) {
    SideTrace::WriteActivity(timeui);
}

void VerilatedSide::makeNameMap() {
    // Take signal information from each module and build m_namemapp
    deleteNameMap();
    m_namemapp = new NameMap;

    Super::traceInit();

    // Though not speced, it's illegal to generate a side file with signals
    // not under any module - it crashes at least two viewers.
    // If no scope was specified, prefix everything with a "top"
    // This comes from user instantiations with no name - IE Vtop("").
    bool nullScope = false;
    for (const auto& i : *m_namemapp) {
        const std::string& hiername = i.first;
        if (!hiername.empty() && hiername[0] == '\t') nullScope = true;
    }
    if (nullScope) {
        NameMap* const newmapp = new NameMap;
        for (const auto& i : *m_namemapp) {
            const std::string& hiername = i.first;
            const std::string& decl = i.second;
            std::string newname{"top"};
            if (hiername[0] != '\t') newname += ' ';
            newname += hiername;
            newmapp->emplace(newname, decl);
        }
        deleteNameMap();
        m_namemapp = newmapp;
    }
}

void VerilatedSide::deleteNameMap() {
    if (m_namemapp) VL_DO_CLEAR(delete m_namemapp, m_namemapp = nullptr);
}

VerilatedSide::~VerilatedSide() {
    close();
    // Write validation output before closing
    if (SideTrace::kNumInstances > 0) {
        std::string validation_output = SideTrace::generateValidationOutput();
        if (!validation_output.empty()) {
            // Open the file in append mode, write, then close
            std::ofstream ofs(m_filename, std::ios::app);
            if (ofs.is_open()) {
                ofs << validation_output;
                ofs.close();
            }
        }
    }
    if (m_wrBufp) VL_DO_CLEAR(delete[] m_wrBufp, m_wrBufp = nullptr);
    deleteNameMap();
    if (m_filep && m_fileNewed) VL_DO_CLEAR(delete m_filep, m_filep = nullptr);
    if (parallel()) {
        assert(m_numBuffers == m_freeBuffers.size());
        for (auto& pair : m_freeBuffers) VL_DO_CLEAR(delete[] pair.first, pair.first = nullptr);
    }
}

void VerilatedSide::closePrev() {
    // This function is on the flush() call path
    if (!isOpen()) return;

    Super::flushBase();
    bufferFlush();
    m_isOpen = false;
    m_filep->close();
}

void VerilatedSide::closeErr() {
    // This function is on the flush() call path
    // Close due to an error.  We might abort before even getting here,
    // depending on the definition of vl_fatal.
    if (!isOpen()) return;

    // No buffer flush, just fclose
    m_isOpen = false;
    m_filep->close();  // May get error, just ignore it
}

void VerilatedSide::close() VL_MT_SAFE_EXCLUDES(m_mutex) {
    // This function is on the flush() call path
    const VerilatedLockGuard lock{m_mutex};
    if (!isOpen()) return;
    
    closePrev();
    // closePrev() called Super::flush(), so we just
    // need to shut down the tracing thread here.
    Super::closeBase();

    // Close side channel files and write final closing brace
    for (int i = 0; i < SideTrace::kNumInstances; ++i) {
        if (SideTrace::output_files[i].is_open()) {
            // Remove trailing comma and newline, then close JSON
            SideTrace::output_files[i].seekp(-2, std::ios_base::end);
            SideTrace::output_files[i] << "\n}\n";
            SideTrace::output_files[i].close();
        }
    }
}

void VerilatedSide::flush() VL_MT_SAFE_EXCLUDES(m_mutex) {
    const VerilatedLockGuard lock{m_mutex};
    Super::flushBase();
    bufferFlush();
}

void VerilatedSide::printStr(const char* str) {
    // Not fast...
    while (*str) {
        *m_writep++ = *str++;
        bufferCheck();
    }
}

void VerilatedSide::printQuad(uint64_t n) {
    constexpr size_t LEN_STR_QUAD = 40;
    char buf[LEN_STR_QUAD];
    VL_SNPRINTF(buf, LEN_STR_QUAD, "%" PRIu64, n);
    printStr(buf);
}

void VerilatedSide::bufferResize(size_t minsize) {
    // minsize is size of largest write.  We buffer at least 8 times as much data,
    // writing when we are 3/4 full (with thus 2*minsize remaining free)
    if (VL_UNLIKELY(minsize > m_wrChunkSize)) {
        const char* oldbufp = m_wrBufp;
        m_wrChunkSize = roundUpToMultipleOf<1024>(minsize * 2);
        m_wrBufp = new char[m_wrChunkSize * 8];
        std::memcpy(m_wrBufp, oldbufp, m_writep - oldbufp);
        m_writep = m_wrBufp + (m_writep - oldbufp);
        m_wrFlushp = m_wrBufp + m_wrChunkSize * 6;
        VL_DO_CLEAR(delete[] oldbufp, oldbufp = nullptr);
    }
}

void VerilatedSide::bufferFlush() VL_MT_UNSAFE_ONE {
    // This function can be called from the trace offload thread
    // This function is on the flush() call path
    // We add output data to m_writep.
    // When it gets nearly full we dump it using this routine which calls write()
    // This is much faster than using buffered I/O
    if (VL_UNLIKELY(!isOpen())) return;
    const char* wp = m_wrBufp;
    while (true) {
        const ssize_t remaining = (m_writep - wp);
        if (remaining == 0) break;
        errno = 0;
        const ssize_t got = m_filep->write(wp, remaining);
        if (got > 0) {
            wp += got;
            m_wroteBytes += got;
        } else if (VL_UNCOVERABLE(got < 0)) {
            if (VL_UNCOVERABLE(errno != EAGAIN && errno != EINTR)) {
                // LCOV_EXCL_START
                // write failed, presume error (perhaps out of disk space)
                const std::string msg
                    = std::string{"VerilatedSide::bufferFlush: "} + std::strerror(errno);
                VL_FATAL_MT("", 0, "", msg.c_str());
                closeErr();
                break;
                // LCOV_EXCL_STOP
            }
        }
    }

    // Reset buffer
    m_writep = m_wrBufp;
}

//=============================================================================
// Side string code

char* VerilatedSide::writeCode(char* writep, uint32_t code) {
    *writep++ = static_cast<char>('!' + code % 94);
    code /= 94;
    while (code) {
        --code;
        *writep++ = static_cast<char>('!' + code % 94);
        code /= 94;
    }
    return writep;
}

//=============================================================================
// Definitions

void VerilatedSide::printIndent(int level_change) {
    if (level_change < 0) m_modDepth += level_change;
    assert(m_modDepth >= 0);
    for (int i = 0; i < m_modDepth; i++) printStr(" ");
    if (level_change > 0) m_modDepth += level_change;
}

void VerilatedSide::dumpHeader() {
    printStr("$version Generated by VerilatedSide $end\n");

    // Verilator used to put in a $date here.  Although $date is shown in
    // IEEE examples, and it is common in VCD writers, VCD readers don't
    // seem to care about it.  Thus, we omit the $date so artifacts are
    // more likely to be reproducible.  If use cases show up that require
    // the $date command to be present, it could be re-added with support
    // for the SOURCE_DATE_EPOCH hook.

    printStr("$timescale ");
    printStr(timeResStr().c_str());  // lintok-begin-on-ref
    printStr(" $end\n");

    makeNameMap();

    // Signal header
    assert(m_modDepth == 0);
    printIndent(1);
    printStr("\n");

    // We detect the spaces in module names to determine hierarchy.  This
    // allows signals to be declared without fixed ordering, which is
    // required as Verilog signals might be separately declared from
    // SC module signals.

    // Print the signal names
    const char* lastName = "";
    for (const auto& i : *m_namemapp) {
        const std::string& hiernamestr = i.first;
        const std::string& decl = i.second;

        // Determine difference between the old and new names
        const char* const hiername = hiernamestr.c_str();
        const char* lp = lastName;
        const char* np = hiername;
        lastName = hiername;

        // Skip common prefix, it must break at a space or tab
        for (; *np && (*np == *lp); np++, lp++) {}
        while (np != hiername && *np && *np != ' ' && *np != '\t') {
            --np;
            --lp;
        }
        // printf("hier %s\n  lp=%s\n  np=%s\n",hiername,lp,np);

        // Any extra spaces in last name are scope ups we need to do
        bool first = true;
        for (; *lp; lp++) {
            if (*lp == ' ' || (first && *lp != '\t')) {
                printIndent(-1);
                printStr("$upscope $end\n");
            }
            first = false;
        }

        // Any new spaces are scope downs we need to do
        while (*np) {
            if (*np == ' ') np++;
            if (*np == '\t') break;  // tab means signal name starts
            printIndent(1);
            // Find character after name end
            const char* sp = np;
            while (*sp && *sp != ' ' && *sp != '\t' && !(*sp & '\x80')) sp++;

            printStr("$scope ");
            if (*sp & '\x80') {
                switch (*sp & 0x7f) {
                case VLT_TRACE_SCOPE_STRUCT: printStr("struct "); break;
                case VLT_TRACE_SCOPE_INTERFACE: printStr("interface "); break;
                case VLT_TRACE_SCOPE_UNION: printStr("union "); break;
                default: printStr("module ");
                }
            } else {
                printStr("module ");
            }

            for (; *np && *np != ' ' && *np != '\t'; np++) {
                if (*np == '[') {
                    printStr("[");
                } else if (*np == ']') {
                    printStr("]");
                } else if (!(*np & '\x80')) {
                    *m_writep++ = *np;
                }
            }
            printStr(" $end\n");
        }

        printIndent(0);
        printStr(decl.c_str());
    }

    while (m_modDepth > 1) {
        printIndent(-1);
        printStr("$upscope $end\n");
    }

    printIndent(-1);
    printStr("$enddefinitions $end\n\n\n");
    assert(m_modDepth == 0);

    // Validate configuration after all signals have been processed
    SideTrace::validateConfiguration(m_filep);

    // Reclaim storage
    deleteNameMap();
}
//=============================================================================
//=============================================================================

void VerilatedSide::declare(uint32_t code, const char* name, const char* wirep, bool array,
                           int arraynum, bool tri, bool bussed, int msb, int lsb) {
    const int bits = ((msb > lsb) ? (msb - lsb) : (lsb - msb)) + 1;

    const bool enabled = Super::declCode(code, name, bits, tri);

    if (m_suffixes.size() <= nextCode() * VL_TRACE_SUFFIX_ENTRY_SIZE) {
        m_suffixes.resize(nextCode() * VL_TRACE_SUFFIX_ENTRY_SIZE * 2, 0);
    }

    // Keep upper bound on bytes a single signal can emit into the buffer
    m_maxSignalBytes = std::max<size_t>(m_maxSignalBytes, bits + 32);
    // Make sure write buffer is large enough, plus header
    bufferResize(m_maxSignalBytes + 1024);

    if (!enabled) return;

    // Split name into basename
    // Spaces and tabs aren't legal in VCD signal names, so:
    // Space separates each level of scope
    // Tab separates final scope from signal name
    // Tab sorts before spaces, so signals nicely will print before scopes
    // Note the hiername may be nothing, if so we'll add "\t{name}"
    std::string nameasstr = namePrefix() + name;
    std::string hiername;
    std::string basename;

    for (const auto& name : SideTrace::instance_names) {
        // Match only if 'nameasstr' contains 'name' as a whole word (not as a substring of another word)
        // We check that the match is either at the start or preceded by a space,
        // and is either at the end or followed by a space/tab/NULL.
        size_t pos = nameasstr.find(name);
        while (pos != std::string::npos) {
            bool at_start = (pos == 0) || (nameasstr[pos - 1] == ' ');
            size_t after = pos + name.length();
            bool at_end = (after == nameasstr.length()) ||
                          (nameasstr[after] == ' ') ||
                          (nameasstr[after] == '\t') ||
                          (nameasstr[after] == '\0');
            if (at_start && at_end) {
                size_t module_index = &name - &SideTrace::instance_names[0];
                SideTrace::filtered_signals[module_index][code] = nameasstr;
                SideTrace::modules_found[module_index] = true;  // Mark module as found
                break;
            }
            pos = nameasstr.find(name, pos + 1);
        }
    }

    if (!SideTrace::trigger_signal_name.empty() && nameasstr.find(SideTrace::trigger_signal_name) != std::string::npos) {
        SideTrace::trigger_data_code = code;
        SideTrace::trigger_signal_found = true;  // Mark trigger as found
    }

    for (const char* cp = nameasstr.c_str(); *cp; cp++) {
        if (isScopeEscape(*cp)) {
            // Ahh, we've just read a scope, not a basename
            if (!hiername.empty()) hiername += " ";
            hiername += basename;
            basename = "";
        } else {
            basename += *cp;
        }
    }
    hiername += "\t" + basename;

    // Print reference
    std::string decl = "$var ";
    decl += wirep;  // usually "wire"

    constexpr size_t bufsize = 1000;
    char buf[bufsize];
    VL_SNPRINTF(buf, bufsize, " %2d ", bits);
    decl += buf;
    // Add string code to decl
    char* const endp = writeCode(buf, code);
    *endp = '\0';
    decl += buf;
    // Build suffix array entry
    char* const entryp = &m_suffixes[code * VL_TRACE_SUFFIX_ENTRY_SIZE];
    const size_t length = endp - buf;
    assert(length <= VL_TRACE_MAX_VCD_CODE_SIZE);
    // 1 bit values don't have a ' ' separator between value and string code
    const bool isBit = bits == 1;
    entryp[0] = ' ';  // Separator
    // Use memcpy as we checked size above, and strcpy is flagged unsafe
    std::memcpy(entryp + !isBit, buf,
                std::strlen(buf));  // Code (overwrite separator if isBit)
    entryp[length + !isBit] = '\n';  // Replace '\0' with line termination '\n'
    // Set length of suffix (used to increment write pointer)
    entryp[VL_TRACE_SUFFIX_ENTRY_SIZE - 1] = static_cast<char>(length + !isBit + 1);
    decl += " ";
    decl += basename;
    if (array) {
        VL_SNPRINTF(buf, bufsize, "[%d]", arraynum);
        decl += buf;
        hiername += buf;
    }
    if (bussed) {
        VL_SNPRINTF(buf, bufsize, " [%d:%d]", msb, lsb);
        decl += buf;
    }
    decl += " $end\n";
    m_namemapp->emplace(hiername, decl);
}

void VerilatedSide::declEvent(uint32_t code, const char* name, bool array, int arraynum) {
    declare(code, name, "event", array, arraynum, false, false, 0, 0);
}
void VerilatedSide::declBit(uint32_t code, const char* name, bool array, int arraynum) {
    declare(code, name, "wire", array, arraynum, false, false, 0, 0);
}
void VerilatedSide::declBus(uint32_t code, const char* name, bool array, int arraynum, int msb,
                           int lsb) {
    declare(code, name, "wire", array, arraynum, false, true, msb, lsb);
}
void VerilatedSide::declQuad(uint32_t code, const char* name, bool array, int arraynum, int msb,
                            int lsb) {
    declare(code, name, "wire", array, arraynum, false, true, msb, lsb);
}
void VerilatedSide::declArray(uint32_t code, const char* name, bool array, int arraynum, int msb,
                             int lsb) {
    declare(code, name, "wire", array, arraynum, false, true, msb, lsb);
}
void VerilatedSide::declDouble(uint32_t code, const char* name, bool array, int arraynum) {
    declare(code, name, "real", array, arraynum, false, false, 63, 0);
}

//=============================================================================
// Get/commit trace buffer

VerilatedSide::Buffer* VerilatedSide::getTraceBuffer() {
    VerilatedSide::Buffer* const bufp = new Buffer{*this};
    if (parallel()) {
        // Note: This is called from VerilatedSide::dump, which already holds the lock
        // If no buffer available, allocate a new one
        if (m_freeBuffers.empty()) {
            constexpr size_t pageSize = 4096;
            // 4 * m_maxSignalBytes, so we can reserve 2 * m_maxSignalBytes at the end for safety
            size_t startingSize = roundUpToMultipleOf<pageSize>(4 * m_maxSignalBytes);
            m_freeBuffers.emplace_back(new char[startingSize], startingSize);
            ++m_numBuffers;
        }
        // Grab a buffer
        const auto pair = m_freeBuffers.back();
        m_freeBuffers.pop_back();
        // Initialize
        bufp->m_writep = bufp->m_bufp = pair.first;
        bufp->m_size = pair.second;
        bufp->adjustGrowp();
    }
    // Return the buffer
    return bufp;
}

void VerilatedSide::commitTraceBuffer(VerilatedSide::Buffer* bufp) {
    if (parallel()) {
        // Note: This is called from VerilatedSide::dump, which already holds the lock
        // Resize output buffer. Note, we use the full size of the trace buffer, as
        // this is a lot more stable than the actual occupancy of the trace buffer.
        // This helps us to avoid re-allocations due to small size changes.
        bufferResize(bufp->m_size);
        // Compute occupancy of buffer
        const size_t usedSize = bufp->m_writep - bufp->m_bufp;
        // Copy to output buffer
        std::memcpy(m_writep, bufp->m_bufp, usedSize);
        // Adjust write pointer
        m_writep += usedSize;
        // Flush if necessary
        bufferCheck();
        // Put buffer back on free list
        m_freeBuffers.emplace_back(bufp->m_bufp, bufp->m_size);
    } else {
        // Needs adjusting for emitTimeChange
        m_writep = bufp->m_writep;
    }
    delete bufp;
}

//=============================================================================
// VerilatedSideBuffer implementation

//=============================================================================
// Trace rendering primitives

static void VerilatedSideCCopyAndAppendNewLine(char* writep,
                                              const char* suffixp) VL_ATTR_NO_SANITIZE_ALIGN;

static void VerilatedSideCCopyAndAppendNewLine(char* writep, const char* suffixp) {
    // Copy the whole suffix (this avoid having hard to predict branches which
    // helps a lot). Note: The maximum length of the suffix is
    // VL_TRACE_MAX_VCD_CODE_SIZE + 2 == 7, but we unroll this here for speed.
#ifdef VL_X86_64
    // Copy the whole 8 bytes in one go, this works on little-endian machines
    // supporting unaligned stores.
    *reinterpret_cast<uint64_t*>(writep) = *reinterpret_cast<const uint64_t*>(suffixp);
#else
    // Portable variant
    writep[0] = suffixp[0];
    writep[1] = suffixp[1];
    writep[2] = suffixp[2];
    writep[3] = suffixp[3];
    writep[4] = suffixp[4];
    writep[5] = suffixp[5];
    writep[6] = '\n';  // The 6th index is always '\n' if it's relevant, no need to fetch it.
#endif
}

void VerilatedSideBuffer::finishLine(uint32_t code, char* writep) {
    const char* const suffixp = m_suffixes + code * VL_TRACE_SUFFIX_ENTRY_SIZE;
    VL_DEBUG_IFDEF(assert(suffixp[0]););
    VerilatedSideCCopyAndAppendNewLine(writep, suffixp);

    // Now write back the write pointer incremented by the actual size of the
    // suffix, which was stored in the last byte of the suffix buffer entry.
    m_writep = writep + suffixp[VL_TRACE_SUFFIX_ENTRY_SIZE - 1];

    if (m_owner.parallel()) {
        // Double the size of the buffer if necessary
        if (VL_UNLIKELY(m_writep >= m_growp)) {
            // Compute occupied size of current buffer
            const size_t usedSize = m_writep - m_bufp;
            // We are always doubling the size
            m_size *= 2;
            // Allocate the new buffer
            char* const newBufp = new char[m_size];
            // Copy from current buffer to new buffer
            std::memcpy(newBufp, m_bufp, usedSize);
            // Delete current buffer
            delete[] m_bufp;
            // Make new buffer the current buffer
            m_bufp = newBufp;
            // Adjust write pointer
            m_writep = m_bufp + usedSize;
            // Adjust resize limit
            adjustGrowp();
        }
    } else {
        // Flush the write buffer if there's not enough space left for new information
        // We only call this once per vector, so we need enough slop for a very wide "b###" line
        if (VL_UNLIKELY(m_writep > m_wrFlushp)) {
            m_owner.m_writep = m_writep;
            m_owner.bufferFlush();
            m_writep = m_owner.m_writep;
        }
    }
}

//=============================================================================
// emit* trace routines

// Note: emit* are only ever called from one place (full* in
// verilated_trace_imp.h, which is included in this file at the top),
// so always inline them.

/// Filters signals and accumulates switching activity
void VerilatedSideBuffer::handleSwActivity(uint32_t code, uint32_t hd_hw) {
    // Check if this is the trigger signal
    if(code == SideTrace::trigger_data_code) {
        if(hd_hw > 0 && !SideTrace::trigger_data_flag.load()){
            SideTrace::trigger_data_flag.store(true);
            for (int i = 0; i < SideTrace::kNumInstances; ++i) {
                SideTrace::output_files[i] << "\t\"TW_" << SideTrace::time_window << "\": {\n";
            }
            SideTrace::time_window++;
        }
        else if(hd_hw == 0 && SideTrace::trigger_data_flag.load()){
            // Write accumulated switching activity and close time window
            for (int i = 0; i < SideTrace::kNumInstances; ++i) {
                SideTrace::output_files[i] << "\t\t\"switching_activity\": " << SideTrace::switching_activity[i] << "\n\t},\n";
                SideTrace::switching_activity[i] = 0; // Reset for next window
            }
            SideTrace::trigger_data_flag.store(false);
        }
        return;
    }
    
    // Always monitor switching activity when configured
    if (!SideTrace::monitor_enabled.load()) {
        return; // Skip if not configured
    }

    // Accumulate switching activity for signals in monitored modules
    for (int i = 0; i < SideTrace::kNumInstances; ++i) {
        if (SideTrace::filtered_signals[i].find(code) != SideTrace::filtered_signals[i].end()) {
            SideTrace::switching_activity[i] += hd_hw;
        }
    }
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitEventSide(uint32_t code, VlEvent newval, VlEvent oldval) {
    const bool triggered = newval.isTriggered();
    // TODO : It seems that untriggered events are not filtered
    // should be tested before this last step
    if (triggered) {
        // Don't prefetch suffix as it's a bit too late;
        char* wp = m_writep;
        *wp++ = '1';
        finishLine(code, wp);
    }
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitBitSide(uint32_t code, CData newval, CData oldval) {
    // Calculate switching activity based on configuration
    CData sw_activity;
    if (SideTrace::use_hamming_weight) {
        sw_activity = newval;  // Use newval directly for hamming weight
    } else {
        sw_activity = newval ^ oldval;  // XOR for hamming distance
    }
    sw_activity = __builtin_popcount(sw_activity); // Count 1s
    handleSwActivity(code, sw_activity);
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitCDataSide(uint32_t code, CData newval, int bits, CData oldval) {
    // Calculate switching activity based on configuration
    CData sw_activity;
    if (SideTrace::use_hamming_weight) {
        sw_activity = newval;  // Use newval directly for hamming weight
    } else {
        sw_activity = newval ^ oldval;  // XOR for hamming distance
    }
    sw_activity = __builtin_popcount(sw_activity); // Count 1s
    handleSwActivity(code, sw_activity);
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitSDataSide(uint32_t code, SData newval, int bits, SData oldval) {
    // Calculate switching activity based on configuration
    SData sw_activity;
    if (SideTrace::use_hamming_weight) {
        sw_activity = newval;  // Use newval directly for hamming weight
    } else {
        sw_activity = newval ^ oldval;  // XOR for hamming distance
    }
    sw_activity = __builtin_popcount(sw_activity); // Count 1s
    handleSwActivity(code, sw_activity);
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitIDataSide(uint32_t code, IData newval, int bits, IData oldval) {
    // Calculate switching activity based on configuration
    IData sw_activity;
    if (SideTrace::use_hamming_weight) {
        sw_activity = newval;  // Use newval directly for hamming weight
    } else {
        sw_activity = newval ^ oldval;  // XOR for hamming distance
    }
    sw_activity = __builtin_popcount(sw_activity); // Count 1s
    handleSwActivity(code, sw_activity);
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitQDataSide(uint32_t code, QData newval, int bits, QData oldval) {
    // Calculate switching activity based on configuration
    QData sw_activity;
    if (SideTrace::use_hamming_weight) {
        sw_activity = newval;  // Use newval directly for hamming weight
    } else {
        sw_activity = newval ^ oldval;  // XOR for hamming distance
    }
    sw_activity = __builtin_popcountll(sw_activity); // Count 1s
    handleSwActivity(code, sw_activity);
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitWDataSide(uint32_t code, const WData* newvalp, int bits, const WData* oldvalp) {
    // Calculate switching activity based on configuration
    uint32_t total_activity = 0;
    int words = VL_WORDS_I(bits);
    for (int i = 0; i < words; ++i) {
        WData sw_activity;
        if (SideTrace::use_hamming_weight) {
            sw_activity = newvalp[i];  // Use newval directly for hamming weight
        } else {
            sw_activity = newvalp[i] ^ oldvalp[i];  // XOR for hamming distance
        }
        total_activity += __builtin_popcount(sw_activity);
    }
    handleSwActivity(code, total_activity);
}

VL_ATTR_ALWINLINE
void VerilatedSideBuffer::emitDouble(uint32_t code, double newval) {
//     char* wp = m_writep;
//     // Buffer can't overflow before VL_SNPRINTF; we sized during declaration
//     VL_SNPRINTF(wp, m_maxSignalBytes, "r%.16g", newval);
//     wp += std::strlen(wp);
//     finishLine(code, wp);
}
