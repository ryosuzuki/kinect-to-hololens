#include <algorithm>
#include <iostream>
#include <map>
#include <asio.hpp>
#include <opus/opus.h>
#include "native/kh_udp_socket.h"
#include "native/kh_packet.h"
#include "helper/soundio_callback.h"

namespace kh
{
int main(std::string ip_address, int port)
{
    constexpr int KINECT_MICROPHONE_SAMPLE_RATE{48000};
    constexpr int STEREO_CHANNEL_COUNT{2};
    constexpr double MICROPHONE_LATENCY{0.2}; // seconds
    constexpr int AUDIO_FRAME_SIZE{960};

    Audio audio;
    auto default_speaker{audio.getDefaultOutputDevice()};
    AudioOutStream default_speaker_stream(default_speaker);
    // These settings are those generic and similar to Azure Kinect's.
    // It is set to be Stereo, which is the default setting of Unity3D.
    default_speaker_stream.get()->format = SoundIoFormatFloat32LE;
    default_speaker_stream.get()->sample_rate = KINECT_MICROPHONE_SAMPLE_RATE;
    default_speaker_stream.get()->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
    default_speaker_stream.get()->software_latency = MICROPHONE_LATENCY;
    default_speaker_stream.get()->write_callback = soundio_callback::write_callback;
    default_speaker_stream.get()->underflow_callback = soundio_callback::underflow_callback;
    default_speaker_stream.open();

    const int bytes_per_second{default_speaker_stream.get()->sample_rate * default_speaker_stream.get()->bytes_per_frame};
    int capacity = MICROPHONE_LATENCY * 2 * bytes_per_second;

    soundio_callback::ring_buffer = soundio_ring_buffer_create(audio.get(), capacity);
    if (!soundio_callback::ring_buffer) {
        printf("unable to create ring buffer\n");
        return 1;
    }

    asio::io_context io_context;
    asio::ip::udp::socket socket(io_context);
    socket.open(asio::ip::udp::v4());
    socket.set_option(asio::socket_base::receive_buffer_size{1024 * 1024});

    UdpSocket udp_socket(std::move(socket), asio::ip::udp::endpoint{asio::ip::address::from_string(ip_address), gsl::narrow_cast<unsigned short>(port)});
    std::error_code asio_error;
    udp_socket.send(create_ping_receiver_packet_bytes(), asio_error);

    AudioDecoder audio_decoder{KINECT_MICROPHONE_SAMPLE_RATE, STEREO_CHANNEL_COUNT};

    default_speaker_stream.start();

    float out[AUDIO_FRAME_SIZE * STEREO_CHANNEL_COUNT];

    std::map<int, std::vector<std::byte>> packets;
    int received_byte_count = 0;
    int last_frame_id = -1;
    auto summary_time = std::chrono::steady_clock::now();
    for (;;) {
        soundio_flush_events(audio.get());

        while (auto packet = udp_socket.receive(asio_error)) {
            received_byte_count += packet->size();

            PacketCursor cursor{5};
            int frame_id = copy_from_bytes<int>(*packet, cursor);
            packets.insert({ frame_id, std::move(*packet) });
        }

        //while (packets.size() > 20) {
        //    printf("remove packet");
        //    packets.erase(packets.begin());
        //}

        char* write_ptr = soundio_ring_buffer_write_ptr(soundio_callback::ring_buffer);
        int free_bytes = soundio_ring_buffer_free_count(soundio_callback::ring_buffer);
        //printf("free_bytes: %d\n", free_bytes);
        //printf("latency: %f\n", ring_buffer->getFillCount() / static_cast<float>(out_stream->sample_rate() * out_stream->bytes_per_frame()));
        //printf("packet size: %ld\n", packets.size());

        const int FRAME_BYTE_SIZE = sizeof(float) * AUDIO_FRAME_SIZE * STEREO_CHANNEL_COUNT;

        int write_cursor = 0;
        auto packet_it = packets.begin();
        while((free_bytes - write_cursor) > FRAME_BYTE_SIZE) {
            if (packet_it == packets.end())
                break;

            auto audio_sender_packet_data{parse_audio_sender_packet_bytes(packet_it->second)};

            int frame_size;
            if (audio_sender_packet_data.frame_id <= last_frame_id) {
                // If a packet is about the past, throw it away and try again.
                packet_it = packets.erase(packet_it);
                continue;
            } else if (audio_sender_packet_data.frame_id == last_frame_id + 1) {
                // When the packet for the next audio frame is found,
                // use it and erase it.
                //int opus_frame_size = copy_from_bytes<int>(packet_it->second, audio_packet_cursor);
                //frame_size = opus_decode_float(opus_decoder, reinterpret_cast<unsigned char*>(packet_it->second.data()) + audio_packet_cursor.position, opus_frame_size, out, AUDIO_FRAME_SIZE, 0);
                //frame_size = opus_decode_float(opus_decoder,
                //                               reinterpret_cast<unsigned char*>(audio_sender_packet_data.opus_frame.data()),
                //                               audio_sender_packet_data.opus_frame.size(),
                //                               out,
                //                               AUDIO_FRAME_SIZE, 0);
                frame_size = audio_decoder.decode(audio_sender_packet_data.opus_frame,
                                                  out,
                                                  AUDIO_FRAME_SIZE, 0);
                packet_it = packets.erase(packet_it);
            } else {
                // If not, let opus know there is a packet loss.
                //frame_size = opus_decode_float(opus_decoder, nullptr, 0, out, AUDIO_FRAME_SIZE, 0);
                frame_size = audio_decoder.decode(std::nullopt, out, AUDIO_FRAME_SIZE, 0);
            }

            if (frame_size < 0) {
                printf("decoder failed: %s\n", opus_strerror(frame_size));
                return 1;
            }

            //printf("frame_id: %d, opus_frame_size: %d, frame_size: %d\n", frame_id, opus_frame_size, frame_size);
            memcpy(write_ptr + write_cursor, out, FRAME_BYTE_SIZE);

            ++last_frame_id;
            write_cursor += FRAME_BYTE_SIZE;
        }

        soundio_ring_buffer_advance_write_ptr(soundio_callback::ring_buffer, write_cursor);

        auto summary_diff = std::chrono::steady_clock::now() - summary_time;
        if (summary_diff > std::chrono::seconds(5))
        {
            printf("Bandwidth: %f Mbps\n", (received_byte_count / (1024.0f * 1024.0f / 8.0f)) / (summary_diff.count() / 1000000000.0f));
            received_byte_count = 0;
            summary_time = std::chrono::steady_clock::now();
        }
    }
    return 0;
}
}

int main()
{
    return kh::main("127.0.0.1", 7777);
}