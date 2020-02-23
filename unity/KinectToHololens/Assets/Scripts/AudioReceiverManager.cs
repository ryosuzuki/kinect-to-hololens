﻿using System;
using System.Net;
using System.Net.Sockets;
using UnityEngine;

public class AudioReceiverManager : MonoBehaviour
{
    private const int AZURE_KINECT_SAMPLE_RATE = 48000;
    private const int AUDIO_FRAME_SIZE = 960;
    private const int STEREO_CHANNEL_COUNT = 2;
    private ReceiverSocket receiver;
    private RingBuffer ringBuffer;
    private OpusDecoder opusDecoder;

    void Start()
    {
        receiver = new ReceiverSocket(1024 * 1024);
        ringBuffer = new RingBuffer(64 * 1024);
        opusDecoder = new OpusDecoder(AZURE_KINECT_SAMPLE_RATE, STEREO_CHANNEL_COUNT);

        IPAddress address = IPAddress.Parse("127.0.0.1");
        int port = 7777;

        receiver.Ping(address, port);
    }

    private void Update()
    {
        SocketError error = SocketError.WouldBlock;
        while (true)
        {
            var packet = receiver.Receive(out error);
            if (packet == null)
                break;

            int cursor = 5;
            int frameId = BitConverter.ToInt32(packet, cursor);
            cursor += 4;

            int opusFrameSize = BitConverter.ToInt32(packet, cursor);
            cursor += 4;

            print($"opusFrameSize: {opusFrameSize}");

            OpusFrame opusFrame = opusDecoder.Decode(packet, cursor, opusFrameSize, AUDIO_FRAME_SIZE, STEREO_CHANNEL_COUNT);
            ringBuffer.Write(opusFrame.GetArray());
        }
    }

    void OnAudioFilterRead(float[] data, int channels)
    {
        ringBuffer.Read(data);
    }
}
