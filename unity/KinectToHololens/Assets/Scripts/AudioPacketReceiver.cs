﻿using System.Collections.Concurrent;
using System.Collections.Generic;

class AudioPacketReceiver
{
    private AudioDecoder audioDecoder;
    private int lastAudioFrameId;

    public AudioPacketReceiver()
    {
        audioDecoder = new AudioDecoder(KinectReceiver.KH_SAMPLE_RATE, KinectReceiver.KH_CHANNEL_COUNT);
        lastAudioFrameId = -1;
    }

    public void Receive(ConcurrentQueue<AudioSenderPacketData> audioPacketDataQueue, RingBuffer ringBuffer)
    {
        var audioPacketDataSet = new List<AudioSenderPacketData>();
        {
            AudioSenderPacketData audioPacketData;
            while (audioPacketDataQueue.TryDequeue(out audioPacketData))
            {
                audioPacketDataSet.Add(audioPacketData);
            }
        }

        audioPacketDataSet.Sort((x, y) => x.frameId.CompareTo(y.frameId));

        float[] pcm = new float[KinectReceiver.KH_SAMPLES_PER_FRAME * KinectReceiver.KH_CHANNEL_COUNT];
        int index = 0;
        while (ringBuffer.FreeSamples >= pcm.Length)
        {
            if (index >= audioPacketDataSet.Count)
                break;

            var audioPacketData = audioPacketDataSet[index++];
            if (audioPacketData.frameId <= lastAudioFrameId)
                continue;

            audioDecoder.Decode(audioPacketData.opusFrame, pcm, KinectReceiver.KH_SAMPLES_PER_FRAME);
            ringBuffer.Write(pcm);
            lastAudioFrameId = audioPacketData.frameId;
        }
    }
}