#include "SpinnakerCamera.h"

#include <iostream>
#include <sstream>

#include "SpinGenApi/SpinnakerGenApi.h"

using namespace Spinnaker;
using namespace Spinnaker::GenApi;
using namespace Spinnaker::GenICam;

std::unordered_map<PixelFormat, std::string> SpinnakerCamera::fmt2str;

namespace {

void GrabNextImageByTrigger(CameraPtr pCam) {
    try {
        // Execute software trigger
        pCam->TriggerSoftware.Execute();

    } catch (Spinnaker::Exception &e) {
        throw std::runtime_error("Error: " + std::string(e.what()));
    }
}

}  // anonymous namespace

// -----------------------------------------------------------------------------
// Public methods
// -----------------------------------------------------------------------------

SpinnakerCamera::SpinnakerCamera() {
    initialize();
}

SpinnakerCamera::~SpinnakerCamera() {
    release();
}

void SpinnakerCamera::release() {
    pCam = NULL;
    camList.Clear();
    system->ReleaseInstance();
}

std::string SpinnakerCamera::info() const {
    const LibraryVersion version = system->GetLibraryVersion();
    std::stringstream ss;

    // Spinnaker library version
    ss << "Spinnaker library version: "
       << version.major << "."
       << version.minor << "."
       << version.type << "."
       << version.build << std::endl << std::endl;

    ss << "*** DEVICE INFORMATION ***" << std::endl;
    
    try {
        INodeMap &nodeMap = pCam->GetTLDeviceNodeMap();

        const CCategoryPtr category = nodeMap.GetNode("DeviceInformation");
        if (IsAvailable(category) && IsReadable(category)) {
            FeatureList_t features;
            category->GetFeatures(features);

            FeatureList_t::const_iterator it;
            for (it = features.begin(); it != features.end(); ++it) {
                CNodePtr pFeatureNode = *it;
                ss << pFeatureNode->GetName() << " : ";
                CValuePtr pValue = static_cast<CValuePtr>(pFeatureNode);
                ss << (IsReadable(pValue) ? pValue->ToString() : "Node not readable");
                ss << std::endl;
            }
        } else {
            throw std::runtime_error("Device control information not available.");
        }
    } catch (Spinnaker::Exception &e) {
        throw std::runtime_error("Error: " + std::string(e.what()));
    }

    return ss.str();
}

void SpinnakerCamera::trigger() {
    try {
        pCam->Init();

        INodeMap &nodeMap = pCam->GetNodeMap();

        configure();

        acquireImage();

        reset();

        pCam->DeInit();
    } catch (Spinnaker::Exception &e) {
        throw std::runtime_error("Error: " + std::string(e.what()));
    }
}

void SpinnakerCamera::setExposureTime(double seconds) {
    m_exposureTime = seconds;
}

void SpinnakerCamera::setAutoGain(bool enable) {
    m_autoGain = enable;
}

void SpinnakerCamera::setAutoWhiteBalance(bool enable) {
    m_autoWhiteBlance = enable;
}

void SpinnakerCamera::setPixelFormat(PixelFormat format, int bitDepth) {
    m_pixelFormat = format;
    m_bitDepth = bitDepth;
}

void SpinnakerCamera::setGamma(double gamma) {
    m_gamma = gamma;
}

// -----------------------------------------------------------------------------
// Private methods
// -----------------------------------------------------------------------------

void SpinnakerCamera::initialize() {
    // Pixel format 
    if (fmt2str.empty()) {
        fmt2str[PixelFormat::Mono] = "Mono";
        fmt2str[PixelFormat::RGB] = "RGB";
        fmt2str[PixelFormat::BayerRG] = "BayerRG";
        fmt2str[PixelFormat::BayerGB] = "BayerGB";
    }

    // Retrieve singleton reference to system object
    system = System::GetInstance();

    // Retrive camera list
    camList = system->GetCameras();

    if (camList.GetSize() == 0) {
        throw std::runtime_error("No camera detected!");
    }

    // Select first camera
    pCam = camList.GetByIndex(0);
}

void SpinnakerCamera::configure() {
    INodeMap &nodeMap = pCam->GetNodeMap();

    try {
        // Trigger mode
        //pCam->TriggerMode = TriggerMode_Off;
        pCam->TriggerMode = TriggerMode_On;
        pCam->TriggerSource = TriggerSource_Software;

        // Pixel format setting
        pCam->PixelFormat = PixelFormat_RGB8;

        // Image offset and size settings
        pCam->OffsetY = pCam->OffsetY.GetMin();
        std::cout << "Offset Y set to " << pCam->OffsetY.GetValue() << "..." << std::endl;
        pCam->OffsetX = pCam->OffsetX.GetMin();
        std::cout << "Offset X set to " << pCam->OffsetX.GetValue() << "..." << std::endl;
        pCam->Width = pCam->Width.GetMax();
        std::cout << "Width set to " << pCam->Width.GetValue() << "..." << std::endl;
        pCam->Height = pCam->Height.GetMax();
        std::cout << "Height set to " << pCam->Height.GetValue() << "..." << std::endl;

        // Exposure time settings
        pCam->ExposureAuto = m_exposureTime < 0.0 ? ExposureAuto_Continuous : ExposureAuto_Off;
        if (m_exposureTime >= 0.0) {
            pCam->ExposureMode = ExposureMode_Timed;
            pCam->ExposureTime = m_exposureTime * 1.0e6;  // sec -> micro sec
            std::cout << "Exposure time: " << pCam->ExposureTime.GetValue() << " micro seconds..." << std::endl;
        }

        // Auto gain
        pCam->GainAuto = m_autoGain ? GainAuto_Continuous : GainAuto_Off;
        std::cout << "Auto gain control: " << (m_autoGain ? "On" : "Off") << std::endl;
        if (!m_autoGain) {
            pCam->Gain = pCam->Gain.GetMax();
            std::cout << "Gain: " << pCam->Gain.GetValue() << " dB" << std::endl;
        }

        // Auto white balancing
        pCam->BalanceWhiteAuto = m_autoWhiteBlance ? BalanceWhiteAuto_Continuous : BalanceWhiteAuto_Off;
        std::cout << "Auto white balance: " << (m_autoWhiteBlance ? "On" : "Off") << std::endl;

        // Gamma correction
        pCam->Gamma = m_gamma;
        std::cout << "Gamma: " << m_gamma << std::endl;

    } catch (Spinnaker::Exception &e) {
        throw std::runtime_error("Error: " + std::string(e.what()));
    }
}

void SpinnakerCamera::reset() {
    INodeMap &nodeMap = pCam->GetNodeMap();

    try {
        // Reset exposure
        pCam->ExposureAuto = ExposureAuto_Continuous;

        // Reset trigger mode
        pCam->TriggerMode = TriggerMode_Off;

    } catch (Spinnaker::Exception &e) {
        throw std::runtime_error("Error: " + std::string(e.what()));
    }
}

void SpinnakerCamera::acquireImage() {
    try {
        pCam->AcquisitionMode = AcquisitionMode_Continuous;
        std::cout << "Acquisition mode: Continuous" << std::endl;

        pCam->BeginAcquisition();

        ImagePtr pResultImage = NULL;
        while (true) {
            try {
                GrabNextImageByTrigger(pCam);
                pResultImage = pCam->GetNextImage(1000);
                break;
            } catch (Spinnaker::Exception &e) {
                std::cerr << e.what() << std::endl;
            }
        }

        if (pResultImage->IsIncomplete()) {
            std::cout << "Image incomplete: "
                << Image::GetImageStatusDescription(pResultImage->GetImageStatus())
                << "..." << std::endl;
        } else {
            ImagePtr convertedImage = pResultImage->Convert(PixelFormat_RGB8, HQ_LINEAR);

            char filename[256];
            sprintf(filename, "image-%.6f.jpg", m_exposureTime);
            convertedImage->Save(filename);
            std::cout << "Image saved at " << filename << std::endl;
        }

        pResultImage->Release();

        pCam->EndAcquisition();

    } catch (Spinnaker::Exception &e) {
        throw std::runtime_error("Error: " + std::string(e.what()));
    }    
}


