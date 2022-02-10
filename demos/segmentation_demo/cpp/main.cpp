/*
// Copyright (C) 2018-2021 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <iomanip>
#include <iostream>
#include <string>

#include <monitors/presenter.h>
#include <utils/ocv_common.hpp>
#include <utils/args_helper.hpp>
#include <utils/slog.hpp>
#include <utils/images_capture.h>
#include <utils/default_flags.hpp>
#include <utils/performance_metrics.hpp>
#include <gflags/gflags.h>

#include <unordered_map>

#include <pipelines/async_pipeline.h>
#include <models/segmentation_model.h>
#include <pipelines/metadata.h>

DEFINE_INPUT_FLAGS
DEFINE_OUTPUT_FLAGS

static const char help_message[] = "Print a usage message.";
static const char model_message[] = "Required. Path to an .xml file with a trained model.";
static const char target_device_message[] = "Optional. Specify the target device to infer on (the list of available devices is shown below). "
"Default value is CPU. Use \"-d HETERO:<comma-separated_devices_list>\" format to specify HETERO plugin. "
"The demo will look for a suitable plugin for a specified device.";
static const char labels_message[] = "Optional. Path to a file with labels mapping.";
static const char custom_cldnn_message[] = "Required for GPU custom kernels. "
"Absolute path to the .xml file with the kernel descriptions.";
static const char custom_cpu_library_message[] = "Required for CPU custom layers. "
"Absolute path to a shared library with the kernel implementations.";
static const char raw_output_message[] = "Optional. Output inference results as mask histogram.";
static const char nireq_message[] = "Optional. Number of infer requests. If this option is omitted, number of infer requests is determined automatically.";
static const char input_resizable_message[] = "Optional. Enables resizable input with support of ROI crop & auto resize.";
static const char num_threads_message[] = "Optional. Number of threads.";
static const char num_streams_message[] = "Optional. Number of streams to use for inference on the CPU or/and GPU in "
"throughput mode (for HETERO and MULTI device cases use format "
"<device1>:<nstreams1>,<device2>:<nstreams2> or just <nstreams>)";
static const char no_show_message[] = "Optional. Don't show output.";
static const char utilization_monitors_message[] = "Optional. List of monitors to show initially.";
static const char output_resolution_message[] = "Optional. Specify the maximum output window resolution "
    "in (width x height) format. Example: 1280x720. Input frame size used by default.";
static const char only_masks_message[] = "Optional. Display only masks. Could be switched by TAB key.";

DEFINE_bool(h, false, help_message);
DEFINE_string(m, "", model_message);
DEFINE_string(d, "CPU", target_device_message);
DEFINE_string(labels, "", labels_message);
DEFINE_string(c, "", custom_cldnn_message);
DEFINE_string(l, "", custom_cpu_library_message);
DEFINE_bool(r, false, raw_output_message);
DEFINE_uint32(nireq, 0, nireq_message);
DEFINE_bool(auto_resize, false, input_resizable_message);
DEFINE_uint32(nthreads, 0, num_threads_message);
DEFINE_string(nstreams, "", num_streams_message);
DEFINE_bool(no_show, false, no_show_message);
DEFINE_string(u, "", utilization_monitors_message);
DEFINE_string(output_resolution, "", output_resolution_message);
DEFINE_bool(only_masks, false, only_masks_message);

/**
* \brief This function shows a help message
*/
static void showUsage() {
    std::cout << std::endl;
    std::cout << "segmentation_demo [OPTION]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << std::endl;
    std::cout << "    -h                        " << help_message << std::endl;
    std::cout << "    -i                        " << input_message << std::endl;
    std::cout << "    -m \"<path>\"               " << model_message << std::endl;
    std::cout << "    -o \"<path>\"               " << output_message << std::endl;
    std::cout << "    -limit \"<num>\"            " << limit_message << std::endl;
    std::cout << "      -l \"<absolute_path>\"    " << custom_cpu_library_message << std::endl;
    std::cout << "          Or" << std::endl;
    std::cout << "      -c \"<absolute_path>\"    " << custom_cldnn_message << std::endl;
    std::cout << "    -d \"<device>\"             " << target_device_message << std::endl;
    std::cout << "    -labels \"<path>\"          " << labels_message << std::endl;
    std::cout << "    -r                        " << raw_output_message << std::endl;
    std::cout << "    -nireq \"<integer>\"        " << nireq_message << std::endl;
    std::cout << "    -auto_resize              " << input_resizable_message << std::endl;
    std::cout << "    -nthreads \"<integer>\"     " << num_threads_message << std::endl;
    std::cout << "    -nstreams                 " << num_streams_message << std::endl;
    std::cout << "    -loop                     " << loop_message << std::endl;
    std::cout << "    -no_show                  " << no_show_message << std::endl;
    std::cout << "    -output_resolution        " << output_resolution_message << std::endl;
    std::cout << "    -u                        " << utilization_monitors_message << std::endl;
    std::cout << "    -only_masks               " << only_masks_message << std::endl;
}


bool ParseAndCheckCommandLine(int argc, char *argv[]) {
    // ---------------------------Parsing and validation of input args--------------------------------------
    gflags::ParseCommandLineNonHelpFlags(&argc, &argv, true);
    if (FLAGS_h) {
        showUsage();
        showAvailableDevices();
        return false;
    }

    if (FLAGS_i.empty()) {
        throw std::logic_error("Parameter -i is not set");
    }

    if (FLAGS_m.empty()) {
        throw std::logic_error("Parameter -m is not set");
    }

    if (!FLAGS_output_resolution.empty() && FLAGS_output_resolution.find("x") == std::string::npos) {
        throw std::logic_error("Correct format of -output_resolution parameter is \"width\"x\"height\".");
    }
    return true;
}

static const Color PASCAL_VOC_COLORS[] = {
    { 0,   0,   0 },
    { 128, 0,   0 },
    { 0,   128, 0 },
    { 128, 128, 0 },
    { 0,   0,   128 },
    { 128, 0,   128 },
    { 0,   128, 128 },
    { 128, 128, 128 },
    { 64,  0,   0 },
    { 192, 0,   0 },
    { 64,  128, 0 },
    { 192, 128, 0 },
    { 64,  0,   128 },
    { 192, 0,   128 },
    { 64,  128, 128 },
    { 192, 128, 128 },
    { 0,   64,  0 },
    { 128, 64,  0 },
    { 0,   192, 0 },
    { 128, 192, 0 },
    { 0,   64,  128 }
};

cv::Mat applyColorMap(cv::Mat input) {
    // Initializing colors array if needed
    static cv::Mat colors;
    static std::mt19937 rng;
    static std::uniform_int_distribution<int> distr(0, 255);

    if (colors.empty()) {
        colors = cv::Mat(256, 1, CV_8UC3);
        std::size_t i = 0;
        for (; i < arraySize(PASCAL_VOC_COLORS); ++i) {
            colors.at<cv::Vec3b>(i, 0) = { PASCAL_VOC_COLORS[i].blue(), PASCAL_VOC_COLORS[i].green(), PASCAL_VOC_COLORS[i].red() };
        }
        for (; i < (std::size_t)colors.cols; ++i) {
            colors.at<cv::Vec3b>(i, 0) = cv::Vec3b(distr(rng), distr(rng), distr(rng));
        }
    }

    // Converting class to color
    cv::Mat out;
    cv::applyColorMap(input, out, colors);
    return out;
}

cv::Mat renderSegmentationData(const ImageResult& result, OutputTransform& outputTransform, bool masks_only) {
    if (!result.metaData) {
        throw std::invalid_argument("Renderer: metadata is null");
    }

    // Input image is stored inside metadata, as we put it there during submission stage
    auto inputImg = result.metaData->asRef<ImageMetaData>().img;

    if (inputImg.empty()) {
        throw std::invalid_argument("Renderer: image provided in metadata is empty");
    }

    // Visualizing result data over source image
    cv::Mat output = masks_only ? applyColorMap(result.resultImage) : inputImg / 2 + applyColorMap(result.resultImage) / 2;
    outputTransform.resize(output);
    return output;
}

void printRawResults(const ImageResult& result, std::vector<std::string> labels) {
    slog::debug << " --------------- Frame # " << result.frameId << " ---------------" << slog::endl;
    slog::debug << "     Class ID     | Pixels | Percentage " << slog::endl;

    double min_val, max_val;
    cv::minMaxLoc(result.resultImage, &min_val, &max_val);
    int max_classes = static_cast<int>(max_val) + 1; // We use +1 for only background case
    const float range[] = { 0, static_cast<float>(max_classes) };
    const float * ranges[] = { range };
    cv::Mat histogram;
    cv::calcHist(&result.resultImage, 1, 0, cv::Mat(), histogram, 1, &max_classes, ranges);

    const double all = result.resultImage.cols * result.resultImage.rows;
    for (int i = 0; i < max_classes; ++i) {
        const int value = static_cast<int>(histogram.at<float>(i));
        if (value > 0) {
            std::string label = (size_t)i < labels.size() ? labels[i] : "#" + std::to_string(i);
            slog::debug << " "
                << std::setw(16) << std::left << label << " | "
                << std::setw(6) << value << " | "
                << std::setw(5) << std::setprecision(2) << std::fixed << std::right << value / all * 100 << "%"
                << slog::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        PerformanceMetrics metrics, renderMetrics;

        // ------------------------------ Parsing and validation of input args ---------------------------------
        if (!ParseAndCheckCommandLine(argc, argv)) {
            return 0;
        }

        //------------------------------- Preparing Input ------------------------------------------------------
        auto cap = openImagesCapture(FLAGS_i, FLAGS_loop, FLAGS_nireq != 1);
        cv::Mat curr_frame;

        //------------------------------ Running Segmentation routines ----------------------------------------------
        slog::info << *InferenceEngine::GetInferenceEngineVersion() << slog::endl;

        InferenceEngine::Core core;
        AsyncPipeline pipeline(
            std::unique_ptr<SegmentationModel>(new SegmentationModel(FLAGS_m, FLAGS_auto_resize)),
            ConfigFactory::getUserConfig(FLAGS_d, FLAGS_l, FLAGS_c, FLAGS_nireq, FLAGS_nstreams, FLAGS_nthreads),
            core);
        Presenter presenter(FLAGS_u);

        std::vector<std::string> labels;
        if (!FLAGS_labels.empty())
            labels = SegmentationModel::loadLabels(FLAGS_labels);

        bool keepRunning = true;
        int64_t frameNum = -1;
        std::unique_ptr<ResultBase> result;
        uint32_t framesProcessed = 0;
        cv::VideoWriter videoWriter;

        cv::Size outputResolution;
        OutputTransform outputTransform = OutputTransform();
        size_t found = FLAGS_output_resolution.find("x");

        bool only_masks = FLAGS_only_masks;

        while (keepRunning) {
            if (pipeline.isReadyToProcess()) {
                auto startTime = std::chrono::steady_clock::now();

                //--- Capturing frame
                curr_frame = cap->read();

                if (curr_frame.empty()) {
                    // Input stream is over
                    break;
                }

                frameNum = pipeline.submitData(ImageInputData(curr_frame),
                    std::make_shared<ImageMetaData>(curr_frame, startTime));
            }

            if (frameNum == 0) {
                if (found == std::string::npos) {
                    outputResolution = curr_frame.size();
                }
                else {
                    outputResolution = cv::Size{
                        std::stoi(FLAGS_output_resolution.substr(0, found)),
                        std::stoi(FLAGS_output_resolution.substr(found + 1, FLAGS_output_resolution.length()))
                    };
                    outputTransform = OutputTransform(curr_frame.size(), outputResolution);
                    outputResolution = outputTransform.computeResolution();
                }
            }

            // Preparing video writer if needed
            if (!FLAGS_o.empty() && !videoWriter.isOpened()) {
                if (!videoWriter.open(FLAGS_o, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                    cap->fps(), outputResolution)) {
                    throw std::runtime_error("Can't open video writer");
                }
            }

            //--- Waiting for free input slot or output data available. Function will return immediately if any of them are available.
            pipeline.waitForData();

            //--- Checking for results and rendering data if it's ready
            //--- If you need just plain data without rendering - cast result's underlying pointer to ImageResult*
            //    and use your own processing instead of calling renderSegmentationData().
            while (keepRunning && (result = pipeline.getResult())) {
                auto renderingStart = std::chrono::steady_clock::now();
                cv::Mat outFrame = renderSegmentationData(result->asRef<ImageResult>(), outputTransform, only_masks);
                //--- Showing results and device information
                if (FLAGS_r) {
                    printRawResults(result->asRef<ImageResult>(), labels);
                }
                presenter.drawGraphs(outFrame);
                renderMetrics.update(renderingStart);
                metrics.update(result->metaData->asRef<ImageMetaData>().timeStamp,
                    outFrame, { 10, 22 }, cv::FONT_HERSHEY_COMPLEX, 0.65);
                if (videoWriter.isOpened() && (FLAGS_limit == 0 || framesProcessed <= FLAGS_limit - 1)) {
                    videoWriter.write(outFrame);
                }
                framesProcessed++;
                if (!FLAGS_no_show) {
                    cv::imshow("Segmentation Results", outFrame);

                    //--- Processing keyboard events
                    auto key = cv::waitKey(1);
                    if (27 == key || 'q' == key || 'Q' == key) { // Esc
                        keepRunning = false;
                    } else if (9 == key) {
                        only_masks = !only_masks;
                    } else {
                        presenter.handleKey(key);
                    }
                }
            }
        } // while(keepRunning)

        // ------------ Waiting for completion of data processing and rendering the rest of results ---------
        pipeline.waitForTotalCompletion();

        for (; framesProcessed <= frameNum; framesProcessed++) {
            result = pipeline.getResult();
            if (result != nullptr) {
                cv::Mat outFrame = renderSegmentationData(result->asRef<ImageResult>(), outputTransform, only_masks);
                //--- Showing results and device information
                if (FLAGS_r) {
                    printRawResults(result->asRef<ImageResult>(), labels);
                }
                presenter.drawGraphs(outFrame);
                metrics.update(result->metaData->asRef<ImageMetaData>().timeStamp,
                    outFrame, { 10, 22 }, cv::FONT_HERSHEY_COMPLEX, 0.65);
                if (videoWriter.isOpened() && (FLAGS_limit == 0 || framesProcessed <= FLAGS_limit - 1)) {
                    videoWriter.write(outFrame);
                }
                if (!FLAGS_no_show) {
                    cv::imshow("Segmentation Results", outFrame);
                    //--- Updating output window
                    cv::waitKey(1);
                }
            }
        }

        slog::info << "Metrics report:" << slog::endl;
        metrics.logTotal();
        logLatencyPerStage(cap->getMetrics().getTotal().latency, pipeline.getPreprocessMetrics().getTotal().latency,
            pipeline.getInferenceMetircs().getTotal().latency, pipeline.getPostprocessMetrics().getTotal().latency,
            renderMetrics.getTotal().latency);
        slog::info << presenter.reportMeans() << slog::endl;
    }
    catch (const std::exception& error) {
        slog::err << error.what() << slog::endl;
        return 1;
    }
    catch (...) {
        slog::err << "Unknown/internal exception happened." << slog::endl;
        return 1;
    }

    return 0;
}
