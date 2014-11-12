#include "flycapture/FlyCapture2.h"
#include "flycapture/CameraBase.h"
#include "flycapture/Image.h"

#include <iostream>
#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <signal.h>
#include <unistd.h>

class Logger {
  using clock = std::chrono::system_clock;

public:
  Logger() {
    begin = clock::now();
  }

  void error(const FlyCapture2::Error& error) {
    std::cout << "[" <<time_from_begin() << "]" << " [ERROR] Flycapture: " << error.GetDescription() << std::endl;
  }

  void info(const std::string& msg) {
    std::cout << "[" <<time_from_begin() << "]" << " [INFO]: " << msg << std::endl;
  }

  void error(const std::string& msg) {
    std::cout << "[" <<time_from_begin() << "]" << " [ERROR]: " << msg << std::endl;
  }


  long time_from_begin() {
    long diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - begin).count();
    return diff_ms;
  }

  private:
    clock::time_point begin;
};

Logger logger;
bool running;

std::unique_ptr<FlyCapture2::GigECamera> getCameraFromSerialNumber(unsigned int serialNumber)
{
  FlyCapture2::Error error;
  FlyCapture2::BusManager busMgr;
  FlyCapture2::PGRGuid cameraGuid;

  if((error = busMgr.GetCameraFromSerialNumber(serialNumber,  &cameraGuid)) != FlyCapture2::PGRERROR_OK) {
    logger.error(error);
    printf("Unable to get camera with serial number %u\n", serialNumber);
    return nullptr;
  } else {
    logger.info("Got camera");
  }

  auto cam = std::unique_ptr<FlyCapture2::GigECamera>(new FlyCapture2::GigECamera());
  if((error = cam->Connect(&cameraGuid)) != FlyCapture2::PGRERROR_OK) {
    logger.error(error);
    printf("Unable to connect to GigE camera with serial number %u\n", serialNumber);
    return nullptr;
  } else {
    logger.info("Camera connected");
  }

  return cam;
}

void printNumberOfAvailableCameras() {
  FlyCapture2::Error error;
  FlyCapture2::BusManager busMgr;

  unsigned int numOfCameras = 0;

  if((error = busMgr.GetNumOfCameras(&numOfCameras)) != FlyCapture2::PGRERROR_OK) {
    logger.error(error);
    logger.error("Unable to retrieve number of available cameras from bus manager");
  } else {
    printf("found %d cameras\n", numOfCameras);
  }
}

std::vector<int> getSerialsFromArgs(int argc, char** argv) {
  std::vector<int> serials;

  for(int i=1; i < argc; i++) {
    char* str = argv[i];
    int serial;
    if(sscanf(str, "%d", &serial) == 1) {
      serials.push_back(serial);
    }
  }

  return serials;
}

void printUsage() {
  std::cout << "Usage: " << std::endl;
  std::cout << "test {cam0_serial} [cam1_serial...]" << std::endl;
}

void sigintHandler(int) {
  running = false;
}

int main(int argc, char** argv) {
  printNumberOfAvailableCameras();
  signal(SIGINT, sigintHandler);

  auto camSerials = getSerialsFromArgs(argc, argv);

  if(camSerials.size() == 0) {
    printUsage();
    return -1;
  }

  printf("Starting %lu cameras \n", camSerials.size());

  std::vector<std::unique_ptr<FlyCapture2::GigECamera>> cameras;
  for(int serial : camSerials) {
    auto cam = getCameraFromSerialNumber(serial);
    if(cam == nullptr) {
      printf("error while loading camera %d. Aborting.\n", serial);
      exit(-1);
    } else {
      cameras.push_back(std::move(cam));
    }
  }

  for(const std::unique_ptr<FlyCapture2::GigECamera>& cam : cameras) {
    FlyCapture2::Error error;
    if ((error = cam->StartCapture()) != FlyCapture2::PGRERROR_OK) {
      logger.error(error);
      return -1;
    }
  }

  int frameCount = 0;
  running = true;
  while(running) {
    int camIndex = 0;
    for(const std::unique_ptr<FlyCapture2::GigECamera>& cam : cameras) {
      FlyCapture2::Image image;

      FlyCapture2::Error error;
      if ((error = cam->RetrieveBuffer(&image)) != FlyCapture2::PGRERROR_OK) {
        logger.error(error);
        printf("skipping frame %05d for cam n.%d\n", frameCount, camIndex);
        continue;
      }

      char filename[200];
      sprintf(filename, "cam%d_frame_%05d.png", camIndex, frameCount);
      image.Save(filename);

      char msg[200];
      sprintf(msg, "captured frame %s\n", filename);
      logger.info(msg);
    }
  }

  for(const std::unique_ptr<FlyCapture2::GigECamera>& cam : cameras) {
    FlyCapture2::Error error;
    if ((error = cam->StopCapture()) != FlyCapture2::PGRERROR_OK) {
      logger.error(error);
    }
  }

}
