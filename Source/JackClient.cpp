
#include "JackClient.h"

#include <dlfcn.h>
#include <glob.h>

#include <vector>
#include <string>

JackClient::JackClient(MidiQueue& queue)
    : midiQueue(queue)
{
}

JackClient::~JackClient()
{
    stop();
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
    LOAD(activate, "jack_activate");
    LOAD(deactivate, "jack_deactivate");
    LOAD(port_get_buffer, "jack_port_get_buffer");
    LOAD(midi_clear_buffer, "jack_midi_clear_buffer");
    LOAD(midi_event_write, "jack_midi_event_write");
    LOAD(transport_start, "jack_transport_start");
    LOAD(transport_stop, "jack_transport_stop");
    LOAD(transport_locate, "jack_transport_locate");
    LOAD(get_sample_rate, "jack_get_sample_rate");

    #undef LOAD

    if (!client_open ||
        !client_close ||
        !port_register ||
        !set_process_callback ||
        !activate ||
        !deactivate ||
        !port_get_buffer ||
        !midi_clear_buffer ||
        !midi_event_write ||
        !transport_start ||
        !transport_stop ||
        !transport_locate ||
        !get_sample_rate)
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

    library = nullptr;

    client_open = nullptr;
    client_close = nullptr;
    port_register = nullptr;
    set_process_callback = nullptr;
    activate = nullptr;
    deactivate = nullptr;
    port_get_buffer = nullptr;
    midi_clear_buffer = nullptr;
    midi_event_write = nullptr;
    transport_start = nullptr;
    transport_stop = nullptr;
    transport_locate = nullptr;
    get_sample_rate = nullptr;
}

// --------------------------------------------------
// Find and load PipeWire JACK, then system JACK
// --------------------------------------------------

bool JackClient::loadJackApi()
{
    std::vector<std::string> candidates;

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

    for (const auto& path : candidates)
    {
        if (api.load(path.c_str()))
            return true;
    }

    return false;
}

// --------------------------------------------------
// Start
// --------------------------------------------------

bool JackClient::start(const char* clientName)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);

    if (client)
        return true;

    if (!loadJackApi())
        return false;

    jack_status_t status{};

    client = api.client_open(
        clientName,
        JackNoStartServer,
        &status
    );

    if (!client)
    {
        api.unload();
        return false;
    }

    midiOutput = api.port_register(
        client,
        "midi_out",
        JACK_DEFAULT_MIDI_TYPE,
        JackPortIsOutput,
        0
    );

    if (!midiOutput)
    {
        closeClient();
        return false;
    }

    if (api.set_process_callback(
            client,
            processCallback,
            this) != 0)
    {
        closeClient();
        return false;
    }

    if (api.activate(client) != 0)
    {
        closeClient();
        return false;
    }

    connected.store(true, std::memory_order_release);

    return true;
}

//---restart

bool JackClient::restart(const char* clientName)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);

    closeClient();

    if (!loadJackApi())
        return false;

    jack_status_t status{};

    client = api.client_open(
        clientName,
        JackNoStartServer,
        &status
    );

    if (!client)
    {
        api.unload();
        return false;
    }

    midiOutput = api.port_register(
        client,
        "midi_out",
        JACK_DEFAULT_MIDI_TYPE,
        JackPortIsOutput,
        0
    );

    if (!midiOutput)
    {
        closeClient();
        return false;
    }

    if (api.set_process_callback(
            client,
            processCallback,
            this) != 0)
    {
        closeClient();
        return false;
    }

    if (api.activate(client) != 0)
    {
        closeClient();
        return false;
    }

    connected.store(true, std::memory_order_release);

    return true;
}

// --------------------------------------------------
// Stop
// --------------------------------------------------

void JackClient::closeClient()
{
    connected.store(false, std::memory_order_release);

    if (client)
    {
        api.deactivate(client);
        api.client_close(client);
        client = nullptr;
    }

    midiOutput = nullptr;

    api.unload();
}

void JackClient::stop()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);

    if (!client)
    {
        api.unload();
        return;
    }

    closeClient();
}

// --------------------------------------------------
// Connection state
// --------------------------------------------------

bool JackClient::isConnected() const
{
    return connected.load(std::memory_order_acquire);
}

// --------------------------------------------------
// JACK process callback
// --------------------------------------------------

int JackClient::processCallback(
    jack_nframes_t nframes,
    void* userData)
{
    auto* self = static_cast<JackClient*>(userData);

    return self->process(nframes);
}

int JackClient::process(jack_nframes_t nframes)
{
    if (!midiOutput)
        return 0;

    void* buffer = api.port_get_buffer(
        midiOutput,
        nframes
    );

    api.midi_clear_buffer(buffer);

    MidiPacket packet;

    while (midiQueue.pop(packet))
    {
        api.midi_event_write(
            buffer,
            0,
            packet.data,
            packet.size
        );
    }

    return 0;
}

// --------------------------------------------------
// JACK Transport
// --------------------------------------------------

void JackClient::transportStart()
{
    if (client && api.transport_start)
        api.transport_start(client);
}

void JackClient::transportStop()
{
    if (client && api.transport_stop)
        api.transport_stop(client);
}

void JackClient::transportLocate(uint64_t frame)
{
    if (client && api.transport_locate)
    {
        api.transport_locate(
            client,
            static_cast<jack_nframes_t>(frame)
        );
    }
}

uint32_t JackClient::getSampleRate() const
{
    if (client && api.get_sample_rate)
        return api.get_sample_rate(client);

    return 0;
}
