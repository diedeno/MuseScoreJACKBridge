
#pragma once

#include <jack/jack.h>
#include <jack/midiport.h>

#include <atomic>
#include <mutex>

#include "MidiQueue.h"

class JackClient
{
public:
    explicit JackClient(MidiQueue& queue);
    ~JackClient();

    bool start(const char* clientName);
    void stop();
    bool isConnected() const;
    
    bool restart(const char* clientName);
    
    void transportStart();
    void transportStop();
    void transportLocate(uint64_t frame);
    uint32_t getSampleRate() const;

private:
    struct JackApi
    {
        void* library = nullptr;

        decltype(&jack_client_open) client_open = nullptr;
        decltype(&jack_client_close) client_close = nullptr;
        decltype(&jack_port_register) port_register = nullptr;
        decltype(&jack_set_process_callback) set_process_callback = nullptr;
        decltype(&jack_activate) activate = nullptr;
        decltype(&jack_deactivate) deactivate = nullptr;
        decltype(&jack_port_get_buffer) port_get_buffer = nullptr;
        decltype(&jack_midi_clear_buffer) midi_clear_buffer = nullptr;
        decltype(&jack_midi_event_write) midi_event_write = nullptr;
        decltype(&jack_transport_start) transport_start = nullptr;
        decltype(&jack_transport_stop) transport_stop = nullptr;
        decltype(&jack_transport_locate) transport_locate = nullptr;
        decltype(&jack_get_sample_rate) get_sample_rate = nullptr;

        bool load(const char* path);
        void unload();
    };

    bool loadJackApi();
    void closeClient();

    static int processCallback(
        jack_nframes_t nframes,
        void* userData
    );

    int process(jack_nframes_t nframes);

    MidiQueue& midiQueue;

    JackApi api;

    jack_client_t* client = nullptr;
    jack_port_t* midiOutput = nullptr;

    std::atomic<bool> connected{false};

    std::mutex lifecycleMutex;
};

