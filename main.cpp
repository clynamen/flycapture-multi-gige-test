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
#include <thread>
#include <algorithm>
#include <mutex>

#include <signal.h>
#include <unistd.h>

class Logger {
  using clock = std::chrono::system_clock;

public:
  Logger() {
    begin = clock::now();
  }

  void error(const FlyCapture2::Error& error) {
    std::cout << "[" << time_from_begin() << "]" << " [ERROR] Flycapture: " << error.GetDescription() << std::endl;
  }

  void info(const std::string& msg) {
    std::cout << "[" << time_from_begin() << "]" << " [INFO]: " << msg << std::endl;
  }

  void error(const std::string& msg) {
    std::cout << "[" << time_from_begin() << "]" << " [ERROR]: " << msg << std::endl;
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
std::mutex flycapMutex;

std::unique_ptr<FlyCapture2::GigECamera> getCameraFromSerialNumber(unsigned int serialNumber)
{
  FlyCapture2::Error error;
  FlyCapture2::BusManager busMgr;
  FlyCapture2::PGRGuid cameraGuid;

  if((error = busMgr.GetCameraFromSerialNumber(serialNumber,  &cameraGuid)) != FlyCapture2::PGRERROR_OK) {
    logger.error(error);
    printf("Unable to get camera with serial number %u\n", serialNumber);
    return nullptr;
  }
  else {
    logger.info("Got camera");
  }

  auto cam = std::unique_ptr<FlyCapture2::GigECamera>(new FlyCapture2::GigECamera());
  if((error = cam->Connect(&cameraGuid)) != FlyCapture2::PGRERROR_OK) {
    logger.error(error);
    printf("Unable to connect to GigE camera with serial number %u\n", serialNumber);
    return nullptr;
  }
  else {
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
  }
  else {
    printf("found %d cameras\n", numOfCameras);
  }
}

std::vector<int> getSerialsFromArgs(int argc, char** argv) {
  std::vector<int> serials;

  for(int i = 1; i < argc; i++) {
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

constexpr std::chrono::milliseconds msSleepForFramerate(int framerate) {
  return std::chrono::milliseconds(1 / framerate);
}

void runCameraWithSerial(int serialNumber) {
  auto cam = getCameraFromSerialNumber(serialNumber);

  if(cam != nullptr) {
    FlyCapture2::Error error;
    printf("starting capture for camera %d\n", serialNumber);

    flycapMutex.lock();
    if((error = cam->StartCapture()) != FlyCapture2::PGRERROR_OK) {
      logger.error(error);
      return;
    } else {
      printf("start capture ok for camera %d\n", serialNumber);
    }
    flycapMutex.unlock();

    int frameCount = 0;
    while(running) {
      FlyCapture2::Image image;

      printf("retrieving frame for camera %d\n", serialNumber);
      flycapMutex.lock();
      if((error = cam->RetrieveBuffer(&image)) != FlyCapture2::PGRERROR_OK) {
        logger.error(error);
        printf("skipping frame %05d for cam with serial %d\n", frameCount, serialNumber);
        continue;
      }
      flycapMutex.unlock();

      char filename[200];
      sprintf(filename, "cam%d_frame_%05d.png", serialNumber, frameCount);
      image.Save(filename);

      char msg[200];
      sprintf(msg, "captured frame %s\n", filename);
      logger.info(msg);

      frameCount++;
      std::this_thread::sleep_for(msSleepForFramerate(40));
    }

    if((error = cam->StopCapture()) != FlyCapture2::PGRERROR_OK) {
      logger.error(error);
    }
  }

  logger.info("closing thread");
}

int main(int argc, char** argv) {
  printNumberOfAvailableCameras();
  signal(SIGINT, sigintHandler);

  auto camSerials = getSerialsFromArgs(argc, argv);

  if(camSerials.size() == 0) {
    printUsage();
    return -1;
  }
  running = true;

  printf("Starting %lu cameras \n", camSerials.size());

  std::vector<std::thread> threads;
  for(int serial : camSerials) {
    std::thread thread([serial]() {runCameraWithSerial(serial);});
    threads.push_back(std::move(thread));
  }

  std::for_each(threads.begin(), threads.end(), [](std::thread& t) {t.join();});

  return 0;
}
