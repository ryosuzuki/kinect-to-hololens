#include "kh_core.h"
#include "kh_sender.h"
#include "kh_vp8.h"
#include "kinect/kinect.h"
#include "azure_kinect/azure_kinect.h"

namespace kh
{
// Sends Azure Kinect frames through a TCP port.
void _send_azure_kinect_frames(int port)
{
    const int TARGET_BITRATE = 2000;
    const int32_t TIMEOUT_IN_MS = 1000;

    std::cout << "Start sending Azure Kinect frames (port: " << port << ")." << std::endl;

    auto device = azure_kinect::obtainAzureKinectDevice();
    if (!device) {
        std::cout << "Could not find an Azure Kinect." << std::endl;
        return;
    }

    k4a_device_configuration_t config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
    config.color_format = K4A_IMAGE_FORMAT_COLOR_YUY2;
    config.color_resolution = K4A_COLOR_RESOLUTION_720P;
    config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
    config.camera_fps = K4A_FRAMES_PER_SECOND_30;

    auto calibration = device->getCalibration(config.depth_mode, config.color_resolution);
    if (!calibration) {
        std::cout << "Failed to receive calibration of the Azure Kinect." << std::endl;
        return;
    }

    // K4A_COLOR_RESOLUTION_720P has a resolution of 1280x720.
    // The bitrate is chosen arbitrarily.
    Vp8Encoder encoder(calibration->color_camera_calibration.resolution_width,
                       calibration->color_camera_calibration.resolution_height,
                       TARGET_BITRATE);

    // Creating a tcp socket with the port and waiting for a connection.
    asio::io_context io_context;
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port));
    auto socket = acceptor.accept();

    std::cout << "Accepted a client!" << std::endl;

    // Sender is a class that will use the socket to send frames to the receiver that has the socket connected to this socket.
    Sender sender(std::move(socket));
    // The sender sends the KinectIntrinsics, so the renderer from the receiver side can prepare rendering Kinect frames.
    // TODO: Add a function send() for Azure Kinect.
    // sender.send(intrinsics);

    if (!device->start(config)) {
        std::cout << "Failed to start the Azure Kinect." << std::endl;
        return;
    }

    // The amount of frames this sender will send before receiveing a feedback from a receiver.
    const int MAXIMUM_FRAME_ID_DIFF = 2;
    // frame_id is the ID of the frame the sender sends.
    int frame_id = 0;
    // receiver_frame_id is the ID that the receiver sent back saying it received the frame of that ID.
    int receiver_frame_id = 0;

    // Variables for profiling the sender.
    auto start = std::chrono::system_clock::now();
    int frame_count = 0;
    size_t frame_size = 0;
    for (;;) {
        // Try getting Kinect images until a valid pair pops up.
        auto capture = device->getCapture(TIMEOUT_IN_MS);
        if (!capture) {
            std::cout << "Could not get a capture from the Azure Kinect." << std::endl;
            return;
        }

        auto color_image = capture->getColorImage();
        if (!color_image)
            continue;

        auto depth_image = capture->getDepthImage();
        if (!depth_image)
            continue;

        // Try receiving a frame ID from the receiver and update receiver_frame_id if possible.
        auto receive_result = sender.receive();
        if (receive_result) {
            int cursor = 0;

            // Currently, message_type is not getting used.
            auto message_type = (*receive_result)[0];
            cursor += 1;

            if (message_type == 0) {
                memcpy(&receiver_frame_id, receive_result->data() + cursor, 4);
            }
        }

        // If more than MAXIMUM_FRAME_ID_DIFF frames are sent to the receiver without receiver_frame_id getting updated,
        // stop sending more.
        if (frame_id - receiver_frame_id > MAXIMUM_FRAME_ID_DIFF)
            continue;

        // Format the color pixels from the Kinect for the Vp8Encoder then encode the pixels with Vp8Encoder.
        //auto yuv_image = createHalvedYuvImageFromKinectColorBuffer(kinect_frame->color_frame()->getRawUnderlyingBuffer());
        //auto vp8_frame = encoder.encode(yuv_image);
        auto yuv_image = createYuvImageFromAzureKinectYuy2Buffer(color_image->getBuffer(),
                                                                 color_image->getWidth(),
                                                                 color_image->getHeight(),
                                                                 color_image->getStride());
        auto vp8_frame = encoder.encode(yuv_image);

        // Compress the depth pixels.
        //auto rvl_frame = createRvlFrameFromKinectDepthBuffer(kinect_frame->depth_frame()->getUnderlyingBuffer());
        auto rvl_frame = createRvlFrameFromKinectDepthBuffer(reinterpret_cast<uint16_t*>(depth_image->getBuffer()),
                                                             depth_image->getWidth(),
                                                             depth_image->getHeight());

        // Print profile measures every 100 frames.
        if (frame_id % 100 == 0) {
            auto end = std::chrono::system_clock::now();
            std::chrono::duration<double> diff = end - start;
            std::cout << "Sending frame " << frame_id
                << ", FPS: " << frame_count / diff.count()
                << ", Bandwidth: " << frame_size / (diff.count() * 131072) // 131072 = 1024 * 1024 / 8
                << " Mbps.\r";
            start = end;
            frame_count = 0;
            frame_size = 0;
        }

        // Try sending the frame. Escape the loop if there is a network error.
        try {
            sender.send(frame_id++, vp8_frame, rvl_frame);
        } catch (std::exception & e) {
            std::cout << e.what() << std::endl;
            break;
        }

        // Updating variables for profiling.
        ++frame_count;
        frame_size += vp8_frame.size() + rvl_frame.size();
    }

    std::cout << "Stopped sending Kinect frames." << std::endl;
}

// Repeats collecting the port number from the user and calling _send_frames() with it.
void send_frames()
{
    for (;;) {
        std::cout << "Enter a port number to start sending frames: ";
        std::string line;
        std::getline(std::cin, line);
        // The default port (the port when nothing is entered) is 7777.
        int port = line.empty() ? 7777 : std::stoi(line);
        try {
            _send_azure_kinect_frames(port);
        } catch (std::exception& e) {
            std::cout << e.what() << std::endl;
        }
    }
}
}

int main()
{
    kh::send_frames();
    return 0;
}