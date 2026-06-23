// -*- mode: C++; c-file-style: "cc-mode" -*-
//=============================================================================
//
// VeriSide: RTL-Level Side-Channel Leakage Tracing Extensions
//
// Author: Behnam Farnaghinejad <behnam.farnaghinejad@polito.it>
// Code available from: https://gitlab.com/smilies-polito/VeriSide
//
// This file is part of VeriSide, a modified version of Verilator for
// power side-channel analysis.
//
// Copyright 2025 by Behnam Farnaghinejad.
//
//=============================================================================
///
/// \file
/// \brief Verilated tracing in SIDE format header
///
/// User wrapper code should use this header when creating SIDE traces.
///
//=============================================================================

#ifndef VERILATOR_VERILATED_SIDE_C_H_
#define VERILATOR_VERILATED_SIDE_C_H_

#include "verilated.h"
#include "verilated_trace.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

class VerilatedSideBuffer;
class VerilatedSideFile;

//=============================================================================

namespace SideTrace {
static constexpr bool kVerboseTrace = false;
constexpr std::streamoff kTrimTrailingComma = -2;

// Dynamic configuration from command line options
extern int kNumInstances;
extern std::vector<std::ofstream> output_files;
extern std::vector<std::string> instance_names;
extern std::vector<std::unordered_map<uint32_t, std::string>> filtered_signals;
extern std::vector<std::uint32_t> switching_activity;
extern std::string trigger_signal_name;

/// Updated atomically in trace callback thread
extern std::atomic<bool> trigger_data_flag;
/// Updated atomically in trace callback thread
extern std::atomic<bool> prev_trigger_flag;
/// Updated atomically in trace callback thread
extern std::atomic<bool> monitor_enabled;
extern std::uint32_t trigger_data_code;
extern std::uint32_t time_window;

// Configuration functions
void configure(const std::vector<std::string>& modules, const std::string& trigger,
               bool hammingWeight = false);
void validateConfiguration(VerilatedSideFile* filep);

inline void WriteActivity(uint64_t timeui) VL_MT_SAFE {
    if (!trigger_data_flag.load()) {

        // If the monitor is not enabled, we do not write anything
        monitor_enabled.store(false);
        if (prev_trigger_flag.load()) { prev_trigger_flag.store(false); }
        return;
    } else {

        if (prev_trigger_flag.load()) {
            for (int i = 0; i < kNumInstances; ++i) {
                output_files[i] << switching_activity[i] << ",\n";
            }
        }
        for (int i = 0; i < kNumInstances; ++i) { switching_activity[i] = 0; }
        prev_trigger_flag.store(true);
        for (int i = 0; i < kNumInstances; ++i) {
            output_files[i] << "\t\t\"" << timeui << "\": ";
        }
        monitor_enabled.store(true);
    }
}
}  // namespace SideTrace
//=============================================================================
// VerilatedSide
// Base class to create a Verilator SIDE dump
// This is an internally used class - see VerilatedSideC for what to call from applications

class VerilatedSide VL_NOT_FINAL : public VerilatedTrace<VerilatedSide, VerilatedSideBuffer> {
public:
    using Super = VerilatedTrace<VerilatedSide, VerilatedSideBuffer>;

private:
    friend VerilatedSideBuffer;  // Give the buffer access to the private bits

    //=========================================================================
    // SIDE specific internals

    VerilatedSideFile* m_filep;  // File we're writing to
    bool m_fileNewed;  // m_filep needs destruction
    bool m_isOpen = false;  // True indicates open file
    std::string m_filename;  // Filename we're writing to (if open)
    uint64_t m_rolloverSize = 0;  // File size to rollover at
    int m_modDepth = 0;  // Depth of module hierarchy

    char* m_wrBufp;  // Output buffer
    char* m_wrFlushp;  // Output buffer flush trigger location
    char* m_writep;  // Write pointer into output buffer
    size_t m_wrChunkSize;  // Output buffer size
    size_t m_maxSignalBytes = 0;  // Upper bound on number of bytes a single signal can generate
    uint64_t m_wroteBytes = 0;  // Number of bytes written to this file

    std::vector<char> m_suffixes;  // SIDE line end string codes + metadata

    using NameMap = std::map<const std::string, const std::string>;
    NameMap* m_namemapp = nullptr;  // List of names for the header

    // Vector of free trace buffers as (pointer, size) pairs.
    std::vector<std::pair<char*, size_t>> m_freeBuffers;
    size_t m_numBuffers = 0;  // Number of trace buffers allocated

    void bufferResize(size_t minsize);
    void bufferFlush() VL_MT_UNSAFE_ONE;
    void bufferCheck() {
        // Flush the write buffer if there's not enough space left for new information
        // We only call this once per vector, so we need enough slop for a very wide "b###" line
        if (VL_UNLIKELY(m_writep > m_wrFlushp)) bufferFlush();
    }
    void openNextImp(bool incFilename);
    void closePrev();
    void closeErr();
    void makeNameMap();
    void deleteNameMap();
    void printIndent(int level_change);
    void printStr(const char* str);
    void printQuad(uint64_t n);
    void printTime(uint64_t timeui);
    void declare(uint32_t code, const char* name, const char* wirep, bool array, int arraynum,
                 bool tri, bool bussed, int msb, int lsb);

    void dumpHeader();

    static char* writeCode(char* writep, uint32_t code);

    // CONSTRUCTORS
    VL_UNCOPYABLE(VerilatedSide);

protected:
    //=========================================================================
    // Implementation of VerilatedTrace interface

    // Called when the trace moves forward to a new time point
    void emitTimeChange(uint64_t timeui) override;

    // Hooks called from VerilatedTrace
    bool preFullDump() override { return isOpen(); }
    bool preChangeDump() override;

    // Trace buffer management
    Buffer* getTraceBuffer() override;
    void commitTraceBuffer(Buffer*) override;

    // Configure sub-class
    void configure(const VerilatedTraceConfig&) override{};

public:
    //=========================================================================
    // External interface to client code

    // CONSTRUCTOR
    VerilatedSide(VerilatedSideFile* filep = nullptr);
    ~VerilatedSide();

    // ACCESSORS
    // Set size in bytes after which new file should be created.
    void rolloverSize(uint64_t size) VL_MT_SAFE { m_rolloverSize = size; }

    // METHODS - All must be thread safe
    // Open the file; call isOpen() to see if errors
    void open(const char* filename) VL_MT_SAFE_EXCLUDES(m_mutex);
    // Open next data-only file
    void openNext(bool incFilename) VL_MT_SAFE_EXCLUDES(m_mutex);
    // Close the file
    void close() VL_MT_SAFE_EXCLUDES(m_mutex);
    // Flush any remaining data to this file
    void flush() VL_MT_SAFE_EXCLUDES(m_mutex);
    // Return if file is open
    bool isOpen() const VL_MT_SAFE { return m_isOpen; }

    //=========================================================================
    // Internal interface to Verilator generated code

    void declEvent(uint32_t code, const char* name, bool array, int arraynum);
    void declBit(uint32_t code, const char* name, bool array, int arraynum);
    void declBus(uint32_t code, const char* name, bool array, int arraynum, int msb, int lsb);
    void declQuad(uint32_t code, const char* name, bool array, int arraynum, int msb, int lsb);
    void declArray(uint32_t code, const char* name, bool array, int arraynum, int msb, int lsb);
    void declDouble(uint32_t code, const char* name, bool array, int arraynum);
};

#ifndef DOXYGEN
// Declare specialization here as it's used in VerilatedFstC just below
template <>
void VerilatedSide::Super::dump(uint64_t time);
template <>
void VerilatedSide::Super::set_time_unit(const char* unitp);
template <>
void VerilatedSide::Super::set_time_unit(const std::string& unit);
template <>
void VerilatedSide::Super::set_time_resolution(const char* unitp);
template <>
void VerilatedSide::Super::set_time_resolution(const std::string& unit);
template <>
void VerilatedSide::Super::dumpvars(int level, const std::string& hier);
#endif  // DOXYGEN

//=============================================================================
// VerilatedSideBuffer

class VerilatedSideBuffer VL_NOT_FINAL {
    // Give the trace file and sub-classes access to the private bits
    friend VerilatedSide;
    friend VerilatedSide::Super;
    friend VerilatedSide::Buffer;
    friend VerilatedSide::OffloadBuffer;

    VerilatedSide& m_owner;  // Trace file owning this buffer. Required by subclasses.

    // Write pointer into output buffer (in parallel mode, this is set up in 'getTraceBuffer')
    char* m_writep = m_owner.parallel() ? nullptr : m_owner.m_writep;
    // Output buffer flush trigger location (only used when not parallel)
    char* const m_wrFlushp = m_owner.parallel() ? nullptr : m_owner.m_wrFlushp;

    // SIDE line end string codes + metadata
    const char* const m_suffixes = m_owner.m_suffixes.data();
    // The maximum number of bytes a single signal can emit
    const size_t m_maxSignalBytes = m_owner.m_maxSignalBytes;

    // Additional data for parallel tracing only
    char* m_bufp = nullptr;  // The beginning of the trace buffer
    size_t m_size = 0;  // The size of the buffer at m_bufp
    char* m_growp = nullptr;  // Resize limit pointer

    void adjustGrowp() {
        m_growp = (m_bufp + m_size) - (2 * m_maxSignalBytes);
        assert(m_growp >= m_bufp + m_maxSignalBytes);
    }

    void finishLine(uint32_t code, char* writep);

    // CONSTRUCTOR
    VerilatedSideBuffer(VerilatedSide& owner)
        : m_owner(owner) {}
    virtual ~VerilatedSideBuffer() = default;

    //=========================================================================
    // Implementation of VerilatedTraceBuffer interface
    // Implementations of duck-typed methods for VerilatedTraceBuffer. These are
    // called from only one place (the full* methods), so always inline them.
    VL_ATTR_ALWINLINE void emitEventSide(uint32_t code, VlEvent newval, VlEvent oldval);
    VL_ATTR_ALWINLINE void emitBitSide(uint32_t code, CData newval, CData oldval);
    VL_ATTR_ALWINLINE void emitCDataSide(uint32_t code, CData newval, int bits, CData oldval = 0);
    VL_ATTR_ALWINLINE void emitSDataSide(uint32_t code, SData newval, int bits, SData oldval = 0);
    VL_ATTR_ALWINLINE void emitIDataSide(uint32_t code, IData newval, int bits, IData oldval = 0);
    VL_ATTR_ALWINLINE void emitQDataSide(uint32_t code, QData newval, int bits, QData oldval = 0);
    VL_ATTR_ALWINLINE void emitWDataSide(uint32_t code, const WData* newvalp, int bits,
                                         const WData* oldvalp = nullptr);
    VL_ATTR_ALWINLINE void emitDouble(uint32_t code, double newval);

    //=========================================================================
    // Private methods
private:
    void handleSwActivity(uint32_t code, uint32_t newval);
};

//=============================================================================
// VerilatedFile
/// Class representing a file to write to. These virtual methods can be
/// overrode for e.g. socket I/O.

class VerilatedSideFile VL_NOT_FINAL {
private:
    int m_fd = 0;  // File descriptor we're writing to
public:
    // METHODS
    /// Construct a (as yet) closed file
    VerilatedSideFile() = default;
    /// Close and destruct
    virtual ~VerilatedSideFile() = default;
    /// Open a file with given filename
    virtual bool open(const std::string& name) VL_MT_UNSAFE;
    /// Close object's file
    virtual void close() VL_MT_UNSAFE;
    /// Write data to file (if it is open)
    virtual ssize_t write(const char* bufp, ssize_t len) VL_MT_UNSAFE;
};

//=============================================================================
// VerilatedSideC
/// Class representing a SIDE dump file in C standalone (no SystemC)
/// simulations.  Also derived for use in SystemC simulations.

class VerilatedSideC VL_NOT_FINAL {
    VerilatedSide m_sptrace;  // Trace file being created

    // CONSTRUCTORS
    VL_UNCOPYABLE(VerilatedSideC);

public:
    /// Construct the dump. Optional argument is a preconstructed file.
    explicit VerilatedSideC(VerilatedSideFile* filep = nullptr)
        : m_sptrace{filep} {}
    /// Destruct, flush, and close the dump
    virtual ~VerilatedSideC() { close(); }

    // METHODS - User called

    /// Return if file is open
    bool isOpen() const VL_MT_SAFE { return m_sptrace.isOpen(); }
    /// Open a new SIDE file
    /// This includes a complete header dump each time it is called,
    /// just as if this object was deleted and reconstructed.
    virtual void open(const char* filename) VL_MT_SAFE { m_sptrace.open(filename); }
    /// Continue a SIDE dump by rotating to a new file name
    /// The header is only in the first file created, this allows
    /// "cat" to be used to combine the header plus any number of data files.
    void openNext(bool incFilename = true) VL_MT_SAFE { m_sptrace.openNext(incFilename); }
    /// Set size in bytes after which new file should be created
    /// This will create a header file, followed by each separate file
    /// which might be larger than the given size (due to chunking and
    /// alignment to a start of a given time's dump).  Any file but the
    /// first may be removed.  Cat files together to create viewable side.
    void rolloverSize(size_t size) VL_MT_SAFE { m_sptrace.rolloverSize(size); }
    /// Close dump
    void close() VL_MT_SAFE { m_sptrace.close(); }
    /// Flush dump
    void flush() VL_MT_SAFE { m_sptrace.flush(); }
    /// Write one cycle of dump data
    /// Call with the current context's time just after eval'ed,
    /// e.g. ->dump(contextp->time())
    void dump(uint64_t timeui) VL_MT_SAFE { m_sptrace.dump(timeui); }
    /// Write one cycle of dump data - backward compatible and to reduce
    /// conversion warnings.  It's better to use a uint64_t time instead.
    void dump(double timestamp) { dump(static_cast<uint64_t>(timestamp)); }
    void dump(uint32_t timestamp) { dump(static_cast<uint64_t>(timestamp)); }
    void dump(int timestamp) { dump(static_cast<uint64_t>(timestamp)); }

    // METHODS - Internal/backward compatible
    // \protectedsection

    // Set time units (s/ms, defaults to ns)
    // Users should not need to call this, as for Verilated models, these
    // propage from the Verilated default timeunit
    void set_time_unit(const char* unit) VL_MT_SAFE { m_sptrace.set_time_unit(unit); }
    void set_time_unit(const std::string& unit) VL_MT_SAFE { m_sptrace.set_time_unit(unit); }
    // Set time resolution (s/ms, defaults to ns)
    // Users should not need to call this, as for Verilated models, these
    // propage from the Verilated default timeprecision
    void set_time_resolution(const char* unit) VL_MT_SAFE { m_sptrace.set_time_resolution(unit); }
    void set_time_resolution(const std::string& unit) VL_MT_SAFE {
        m_sptrace.set_time_resolution(unit);
    }
    // Set variables to dump, using $dumpvars format
    // If level = 0, dump everything and hier is then ignored
    void dumpvars(int level, const std::string& hier) VL_MT_SAFE {
        m_sptrace.dumpvars(level, hier);
    }

    // Internal class access
    VerilatedSide* spTrace() { return &m_sptrace; }
};

#endif  // guard
