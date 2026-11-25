
#pragma once

#include <jack/jack.h>
#include <jack/midiport.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "MidiQueue.h"

// Thin wrapper around a JACK client (JACK2 or pipewire-jack).
//
// libjack is loaded with dlopen so the plugin has no link-time dependency.
// Several candidate libraries are probed and the first one that can actually
// reach a running server is kept. That way the same binary works with
// PipeWire's JACK layer and with a real jackd.
//
// Threading rules:
//  * connect / reconnect / stop run on a non-realtime thread and are
//    serialised by lifecycleMutex.
//  * Nothing else touches the JACK API. The audio thread only sets atomics
//    (requestTransport / requestAllNotesOff) which the JACK process callback
//    applies, so the library can never be unloaded under a running caller.
class JackClient
{
public:
    explicit JackClient(MidiQueue& queue);
    ~JackClient();

    // Make sure a client with this name is connected. Does nothing if one is
    // already connected under that exact name, otherwise (re)opens it.
    bool connect(const std::string& clientName);

    // Always close and reopen.
    bool reconnect(const std::string& clientName);

    void stop();

    bool isConnected() const;

    // Name passed to connect(), and the name JACK actually assigned. They can
    // differ (name taken, sanitised, truncated). Empty while disconnected.
    std::string getRequestedName() const;
    std::string getActualName() const;

    // Cached on connect, safe to call from any thread.
    uint32_t getSampleRate() const;

    // Incremented on every successful (re)connect, so callers can resync.
    uint32_t getConnectionEpoch() const;

    // MIDI events JACK refused (period buffer full).
    uint64_t getWriteFailures() const;

    // Realtime-safe: callable from the host audio thread.
    void requestTransport(bool locate,
                          uint64_t frame,
                          bool stateChanged,
                          bool playing);

    void requestAllNotesOff();

private:
    struct JackApi
    {
        void* library = nullptr;

        decltype(&jack_client_open) client_open = nullptr;
        decltype(&jack_client_close) client_close = nullptr;
        decltype(&jack_port_register) port_register = nullptr;
        decltype(&jack_set_process_callback) set_process_callback = nullptr;
        decltype(&jack_on_shutdown) on_shutdown = nullptr;
        decltype(&jack_activate) activate = nullptr;
        decltype(&jack_deactivate) deactivate = nullptr;
        decltype(&jack_port_get_buffer) port_get_buffer = nullptr;
        decltype(&jack_midi_clear_buffer) midi_clear_buffer = nullptr;
        decltype(&jack_midi_event_write) midi_event_write = nullptr;
        decltype(&jack_transport_start) transport_start = nullptr;
        decltype(&jack_transport_stop) transport_stop = nullptr;
        decltype(&jack_transport_locate) transport_locate = nullptr;
        decltype(&jack_get_sample_rate) get_sample_rate = nullptr;
        decltype(&jack_get_client_name) get_client_name = nullptr;
        decltype(&jack_client_name_size) client_name_size = nullptr;

        bool load(const char* path);
        void unload();
    };

    static std::vector<std::string> libraryCandidates();

    // The following run with lifecycleMutex held.
    bool openClient(const std::string& requested);
    bool openWithLoadedApi(const std::string& requested);
    void closeClient();

    static int processCallback(jack_nframes_t nframes, void* userData);
    static void shutdownCallback(void* userData);

    int process(jack_nframes_t nframes);

    MidiQueue& midiQueue;

    JackApi api;

    jack_client_t* client = nullptr;
    jack_port_t* midiOutput = nullptr;

    std::atomic<bool> connected{false};
    std::atomic<uint32_t> sampleRate{0};
    std::atomic<uint32_t> connectionEpoch{0};
    std::atomic<uint64_t> writeFailures{0};

    // Audio thread -> JACK thread requests.
    std::atomic<uint32_t> pendingFlags{0};
    std::atomic<uint64_t> pendingFrame{0};
    std::atomic<bool> wantPlaying{false};

    mutable std::mutex nameMutex;
    std::string requestedName;
    std::string actualName;

    std::mutex lifecycleMutex;
};
