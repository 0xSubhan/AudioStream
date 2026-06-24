#include <pybind11/pybind11.h>
#include "../include/stream_controller.h"

namespace py = pybind11;

PYBIND11_MODULE(audiostream_core, m) {
    m.doc() = "AudioStream core capture and network streaming module";

    py::class_<audiostream::StreamController>(m, "StreamController")
        .def(py::init<>())
        .def("start", &audiostream::StreamController::start, 
             "Starts the background streaming thread to capture and send audio",
             py::arg("ip"), py::arg("port"))
        .def("stop", &audiostream::StreamController::stop, 
             "Stops the active streaming thread")
        .def("is_streaming", &audiostream::StreamController::isStreaming, 
             "Returns true if the streaming thread is currently active")
        .def("get_latency_ms", &audiostream::StreamController::getLatencyMs, 
             "Returns the current hardware driver capture latency in milliseconds")
        .def("get_packet_count", &audiostream::StreamController::getPacketCount, 
             "Returns the count of packets sent during the current stream session");
}
