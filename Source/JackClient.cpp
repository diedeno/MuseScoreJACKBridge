
#include "JackClient.h"

#include <dlfcn.h>
#include <glob.h>

#include <algorithm>
#include <cstdlib>

namespace
{
    // Shared by all instances in the process: serialises opening of JACK
    // clients.
    std::mutex gOpenMutex;

    constexpr uint32_t kRequestLocate   = 1u << 0;
    constexpr uint32_t kRequestState    = 1u << 1;
    constexpr uint32_t kRequestNotesOff = 1u << 2;

    // JACK client names must not contain ':' (it separates client and port)
    // and have a maximum length. Truncate on a UTF-8 boundary.
    std::string sanitiseClientName(std::string name, size_t maxLen)
    {
        for (auto& c : name)
        {
            if (c == ':' || static_cast<unsigned char>(c) < 0x20)
                c = '-';
        }

        if (name.size() > maxLen)
        {
            size_t len = maxLen;

            while (len > 0
                   && (static_cast<unsigned char>(name[len]) & 0xC0) == 0x80)
                --len;

            name.resize(len);
        }

        if (name.empty())
            name = "MuseScore";

        return name;
    }
}

JackClient::JackClient(MidiQueue& queue)
    : midiQueue(queue)
{
}

JackClient::~JackClient()
{
    stop();

    std::lock_guard<std::mutex> lock(lifecycleMutex);
    api.unload();
}

// --------------------------------------------------
// JACK API loading
// --------------------------------------------------

bool JackClient::JackApi::load(const char* path)
{
    unload();

    library = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (!library)
        return false;

    #define LOAD(member, symbol) \
        member = reinterpret_cast<decltype(member)>( \
            dlsym(library, symbol))

    LOAD(client_open, "jack_client_open");
    LOAD(client_close, "jack_client_close");
    LOAD(port_register, "jack_port_register");
    LOAD(set_process_callback, "jack_set_process_callback");
    LOAD(on_shutdown, "jack_on_shutdown");
    LOAD(activate, "jack_activate");
    LOAD(deactivate, "jack_deactivate");
    LOAD(port_get_buffer, "jack_port_get_buffer");
    LOAD(midi_clear_buffer, "jack_midi_clear_buffer");
    LOAD(midi_event_write, "jack_midi_event_write");
    LOAD(transport_start, "jack_transport_start");
    LOAD(transport_stop, "jack_transport_stop");
    LOAD(transport_locate, "jack_transport_locate");
    LOAD(get_sample_rate, "jack_get_sample_rate");
    LOAD(get_client_name, "jack_get_client_name");
    LOAD(client_name_size, "jack_client_name_size");

    #undef LOAD

    if (!client_open ||
        !client_close ||
        !port_register ||
        !set_process_callback ||
        !on_shutdown ||
        !activate ||
        !deactivate ||
        !port_get_buffer ||
        !midi_clear_buffer ||
        !midi_event_write ||
        !transport_start ||
        !transport_stop ||
        !transport_locate ||
        !get_sample_rate ||
        !get_client_name ||
        !client_name_size)
    {
        unload();
        return false;
    }

    return true;
}

void JackClient::JackApi::unload()
{
    if (library)
        dlclose(library);

    *this = JackApi{};
}

// --------------------------------------------------
// Candidate libjack builds
// --------------------------------------------------
//
// Order:
//   1. $MUSESCORE_JACK_LIB, if set (explicit override)
//   2. PipeWire's libjack (Debian/Ubuntu, Fedora and Arch layouts)
//   3. the system libjack.so.0 (JACK2 / JACK1)
//
// Each one is only kept if it can actually open a client, so a PipeWire
// library on a machine that runs plain jackd is skipped and the system
// library gets its turn, and the other way round.

std::vector<std::string> JackClient::libraryCandidates()
{
    std::vector<std::string> candidates;

    if (const char* overridePath = std::getenv("MUSESCORE_JACK_LIB"))
    {
        if (*overridePath != '\0')
            candidates.emplace_back(overridePath);
    }

    const char* patterns[] = {
        "/usr/lib/*/pipewire-*/jack/libjack.so.0",
        "/usr/lib/pipewire-*/jack/libjack.so.0",
        "/usr/lib64/pipewire-*/jack/libjack.so.0",
        "/lib/*/pipewire-*/jack/libjack.so.0"
    };

    for (const char* pattern : patterns)
    {
        glob_t matches{};

        if (glob(pattern, 0, nullptr, &matches) == 0)
        {
            for (size_t i = 0; i < matches.gl_pathc; ++i)
                candidates.emplace_back(matches.gl_pathv[i]);
        }

        globfree(&matches);
    }

    candidates.emplace_back("libjack.so.0");

    return candidates;
}

// --------------------------------------------------
// Open / close
// --------------------------------------------------

bool JackClient::openWithLoadedApi(const std::string& requested)
{
    const int sizeLimit = api.client_name_size();

    // Leave a few characters free for JACK's "-01" style uniqueness suffix.
    const size_t maxLen = static_cast<size_t>(std::max(8, sizeLimit - 1 - 4));
    const std::string name = sanitiseClientName(requested, maxLen);

    jack_status_t status{};

    client = api.client_open(name.c_str(), JackNoStartServer, &status);

    if (!client)
        return false;

    auto fail = [this]()
    {
        api.client_close(client);
        client = nullptr;
        midiOutput = nullptr;
        return false;
    };

    api.on_shutdown(client, shutdownCallback, this);

    if (api.set_process_callback(client, processCallback, this) != 0)
        return fail();

    midiOutput = api.port_register(
        client,
        "midi_out",
        JACK_DEFAULT_MIDI_TYPE,
        JackPortIsOutput,
        0
    );

    if (!midiOutput)
        return fail();

    // The process callback is not running yet, so draining here is safe.
    // Anything still queued is stale (pushed before a restart).
    MidiPacket stale;
    while (midiQueue.pop(stale)) {}

    pendingFlags.store(0, std::memory_order_relaxed);

    sampleRate.store(
        static_cast<uint32_t>(api.get_sample_rate(client)),
        std::memory_order_relaxed
    );

    {
        const char* assigned = api.get_client_name(client);

        std::lock_guard<std::mutex> lock(nameMutex);
        requestedName = requested;
        actualName = assigned ? assigned : name;
    }

    if (api.activate(client) != 0)
        return fail();

    connectionEpoch.fetch_add(1, std::memory_order_relaxed);
    connected.store(true, std::memory_order_release);

    return true;
}

bool JackClient::openClient(const std::string& requested)
{
    // Fast path: reuse the library that worked last time.
    if (api.library && openWithLoadedApi(requested))
        return true;

    // Otherwise probe the candidates. No client is open here, and the audio
    // thread never calls into libjack, so unloading is safe.
    api.unload();

    for (const auto& path : libraryCandidates())
    {
        if (!api.load(path.c_str()))
            continue;

        if (openWithLoadedApi(requested))
            return true;

        api.unload();
    }

    return false;
}

void JackClient::closeClient()
{
    connected.store(false, std::memory_order_release);

    if (client)
    {
        // Deactivate first: it waits for a running process callback, after
        // which nothing touches midiOutput or the API any more.
        api.deactivate(client);
        api.client_close(client);
        client = nullptr;
    }

    midiOutput = nullptr;

    {
        std::lock_guard<std::mutex> lock(nameMutex);
        actualName.clear();
    }

    // The library stays loaded on purpose; see the class comment.
}

bool JackClient::connect(const std::string& clientName)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);

    if (client
        && connected.load(std::memory_order_acquire)
        && requestedName == clientName)
    {
        return true;
    }

    // Several plugin instances may start at the same moment in one process;
    // open the JACK clients one at a time.
    std::lock_guard<std::mutex> openLock(gOpenMutex);

    closeClient();

    return openClient(clientName);
}

bool JackClient::reconnect(const std::string& clientName)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);
    std::lock_guard<std::mutex> openLock(gOpenMutex);

    closeClient();

    return openClient(clientName);
}

void JackClient::stop()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);

    closeClient();
}

// --------------------------------------------------
// State accessors
// --------------------------------------------------

bool JackClient::isConnected() const
{
    return connected.load(std::memory_order_acquire);
}

std::string JackClient::getRequestedName() const
{
    std::lock_guard<std::mutex> lock(nameMutex);
    return requestedName;
}

std::string JackClient::getActualName() const
{
    std::lock_guard<std::mutex> lock(nameMutex);
    return actualName;
}

uint32_t JackClient::getSampleRate() const
{
    return sampleRate.load(std::memory_order_relaxed);
}

uint32_t JackClient::getConnectionEpoch() const
{
    return connectionEpoch.load(std::memory_order_relaxed);
}

uint64_t JackClient::getWriteFailures() const
{
    return writeFailures.load(std::memory_order_relaxed);
}

uint64_t JackClient::getProcessCalls() const
{
    return processCalls.load(std::memory_order_relaxed);
}

uint64_t JackClient::getEventsWritten() const
{
    return eventsWritten.load(std::memory_order_relaxed);
}

// --------------------------------------------------
// Requests from the audio thread
// --------------------------------------------------

void JackClient::requestTransport(bool locate,
                                  uint64_t frame,
                                  bool stateChanged,
                                  bool playing)
{
    uint32_t bits = 0;

    if (locate)
    {
        pendingFrame.store(frame, std::memory_order_relaxed);
        bits |= kRequestLocate;
    }

    if (stateChanged)
    {
        wantPlaying.store(playing, std::memory_order_relaxed);
        bits |= kRequestState;
    }

    if (bits != 0)
        pendingFlags.fetch_or(bits, std::memory_order_release);
}

void JackClient::requestAllNotesOff()
{
    pendingFlags.fetch_or(kRequestNotesOff, std::memory_order_release);
}

// --------------------------------------------------
// JACK callbacks
// --------------------------------------------------

void JackClient::shutdownCallback(void* userData)
{
    // The server went away. The handle is dead; closeClient() cleans it up
    // the next time connect()/reconnect()/stop() runs.
    static_cast<JackClient*>(userData)
        ->connected.store(false, std::memory_order_release);
}

int JackClient::processCallback(jack_nframes_t nframes, void* userData)
{
    return static_cast<JackClient*>(userData)->process(nframes);
}

int JackClient::process(jack_nframes_t nframes)
{
    processCalls.fetch_add(1, std::memory_order_relaxed);

    if (!midiOutput)
        return 0;

    // Transport requests from the host thread. Locate before start so a
    // rising edge starts from the right position.
    const uint32_t flags =
        pendingFlags.exchange(0, std::memory_order_acquire);

    if (flags & kRequestLocate)
    {
        const uint64_t frame =
            pendingFrame.load(std::memory_order_relaxed);

        api.transport_locate(
            client,
            static_cast<jack_nframes_t>(std::min<uint64_t>(frame, 0xFFFFFFFFu))
        );
    }

    if (flags & kRequestState)
    {
        if (wantPlaying.load(std::memory_order_relaxed))
            api.transport_start(client);
        else
            api.transport_stop(client);
    }

    MidiPacket packet;

    void* buffer = api.port_get_buffer(midiOutput, nframes);

    if (!buffer)
    {
        while (midiQueue.pop(packet)) {}
        return 0;
    }

    api.midi_clear_buffer(buffer);

    while (midiQueue.pop(packet))
    {
        if (api.midi_event_write(buffer, 0, packet.data, packet.size) != 0)
            writeFailures.fetch_add(1, std::memory_order_relaxed);
        else
            eventsWritten.fetch_add(1, std::memory_order_relaxed);
    }

    if (flags & kRequestNotesOff)
    {
        // CC 123 (all notes off) on every channel, so a stop or seek does
        // not leave notes hanging in the external synth.
        for (uint8_t channel = 0; channel < 16; ++channel)
        {
            const uint8_t message[3] = {
                static_cast<uint8_t>(0xB0 | channel), 123, 0
            };

            if (api.midi_event_write(buffer, 0, message, 3) != 0)
                writeFailures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return 0;
}
