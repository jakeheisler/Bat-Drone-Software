#include <stdlib.h>
#include <cstring>
#include <stack>
#include <string>
#include <cmath>
#include <iostream>
#include "autonomy2.hpp"
#include "control.hpp"
#include "control_arbiter.hpp"

using namespace std;

void Autonomy2::threshold(bool out[ROWS][COLS], const uint16_t in[ROWS][COLS]) {
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      out[y][x] = in[y][x] < thresholdHigh && in[y][x] > thresholdLow;
    }
  }
}

list<Autonomy2::Blob> Autonomy2::findBlobs(bool tFrame[ROWS][COLS]) {
  list<Blob> blobs;
  stack<pair<int, int>> next;
  bool visited[ROWS][COLS] = {};
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      visited[y][x] = false;
    }
  }
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      if (tFrame[y][x] && !visited[y][x]) {
        Blob blob;
        int cx = x, cy = y;
        while (true) {
          blob.addPixel(cx, cy);
          if (cx > 0 && tFrame[cy][cx - 1] && !visited[cy][cx - 1]) {
            visited[cy][cx - 1] = true;
            next.push(make_pair(cx - 1, cy));
          }
          if (cy > 0 && tFrame[cy - 1][cx] && !visited[cy - 1][cx]) {
            visited[cy - 1][cx] = true;
            next.push(make_pair(cx, cy - 1));
          }
          if (cx < COLS - 1 && tFrame[cy][cx + 1] && !visited[cy][cx + 1]) {
            visited[cy][cx + 1] = true;
            next.push(make_pair(cx + 1, cy));
          }
          if (cy < ROWS - 1 && tFrame[cy + 1][cx] && !visited[cy + 1][cx]) {
            visited[cy + 1][cx] = true;
            next.push(make_pair(cx, cy + 1));
          }
          if (next.empty()) break;
          cx = next.top().first;
          cy = next.top().second;
          next.pop();
        }
        blob.calculateCentroid();
        blobs.push_front(blob);
      }
    }
  }
  return blobs;
}



void Autonomy2::giveFrame(uint16_t frame[ROWS][COLS]) {
  memcpy(&buffer.getFront(), frame, sizeof(uint16_t[ROWS][COLS]));
  buffer.swapFront();
}

void Autonomy2::giveControl(Control control) {
  receivedControlBuffer.getFront() = control;
  receivedControlBuffer.swapFront();
}

void Autonomy2::giveTarget(int x, int y) {
  receivedTargetBuffer.getFront().x = x;
  receivedTargetBuffer.getFront().y = y;
  receivedTargetBuffer.swapFront();
}

void Autonomy2::givePID(float p1, float i1, float d1, float p2, float i2, float d2, float p3, float i3, float d3, float p4, float i4, float d4) {
  cout << "PIDs" << endl << p1 << " " << i1 << " " << d1 << endl << p2 << " " << i2 << " " << d2 << endl  << p3 << " " << i3 << " " << d3 << endl << p4 << " " << i4 << " " << d4 << endl;
  unique_lock<mutex> ul(pidMutex);
  aPID.p = p1;
  aPID.i = i1;
  aPID.d = d1;
  ePID.p = p2;
  ePID.i = i2;
  ePID.d = d2;
  tPID.p = p3;
  tPID.i = i3;
  tPID.d = d3;
  rPID.p = p4;
  rPID.i = i4;
  rPID.d = d4;
}

void Autonomy2::giveThreshold(uint16_t low, uint16_t high) {
  thresholdLow = low;
  thresholdHigh = high;
}

void Autonomy2::blobListToCmdBlobVect(std::vector<Commands::Blob> &commandBlobs, std::list<Autonomy2::Blob> &blobs) {
  vector<Commands::Blob>::iterator commandBlobsIt = commandBlobs.begin();
  list<Blob>::const_iterator blobsIt = blobs.cbegin();
  while (blobsIt != blobs.cend() && commandBlobsIt != commandBlobs.end()) {
    commandBlobsIt->x = blobsIt->x;
    commandBlobsIt->y = blobsIt->y;
    commandBlobsIt->size = blobsIt->size;
    blobsIt++;
    commandBlobsIt++;
  }
}

Autonomy2::Blob Autonomy2::findLargestBlob(std::list<Autonomy2::Blob> &blobs) {
  list<Blob>::const_iterator largestBlobIt = blobs.cbegin();
  for (list<Blob>::const_iterator i = ++blobs.cbegin(); i != blobs.cend(); i++) {
    if (i->size > largestBlobIt->size) largestBlobIt = i;
  }
  return *largestBlobIt;
}

// Inputs are centerX and centerY
Control Autonomy2::calculateFlightControls(Blob blob) {
  receivedControlBuffer.swapBackIfReady();
  Control con = receivedControlBuffer.getBack();
  //Roll, Yaw, Pitch, Thrust
  float moveRate = 0.2;
  float thrustRate = 0.1;
  int xNullZone = 20, yNullZone = 20;
  float distanceNullZonePercent = 0.2f;
  int sizeNullZone = targetDist * distanceNullZonePercent;
  if (blob.x < 40 - xNullZone) {
    con.aileron = -moveRate;
  } else if (blob.x > 40 + xNullZone) {
    con.aileron = moveRate;
  } else {
    con.aileron = 0;
  }
  if (blob.size < targetDist - sizeNullZone) {
    con.elevator = moveRate;
  } else if (blob.size > targetDist + sizeNullZone) {
    con.elevator = -moveRate;
  } else {
    con.elevator = 0;
  }
  if (con.thrust > -1.0f) {
    if (blob.y < 30 - yNullZone) {
      con.thrust += thrustRate;
      if (con.thrust > 1.0f) con.thrust = 1.0f;
    } else if (blob.y > 30 + yNullZone) {
      con.thrust -= thrustRate;
      if (con.thrust < -1.0f) con.thrust = -1.0f;
    }
  }
  return con;
}
// Inputs are centerX and centerY
Control Autonomy2::calculateFlightControlsPID(Blob blob) {
  receivedControlBuffer.swapBackIfReady();
  Control con = receivedControlBuffer.getBack();
  unique_lock<mutex> ul(pidMutex);
  if (aPID.p != 0 || aPID.i != 0 || aPID.d != 0) con.aileron = aPID.process(blob.x - 40);
  if (ePID.p != 0 || ePID.i != 0 || ePID.d != 0) con.elevator = ePID.process(targetDist - sqrt(blob.size));
  if (tPID.p != 0 || tPID.i != 0 || tPID.d != 0) con.thrust += tPID.process(blob.y - 30);
  if (rPID.p != 0 || rPID.i != 0 || rPID.d != 0) con.rudder = rPID.process(blob.x - 40);
  return con;
}

void Autonomy2::mainLoop(bool & run) {
  while (run) {
	  buffer.swapBack();
    bool tFrame[ROWS][COLS];
	  threshold(tFrame, buffer.getBack());
    list<Blob> blobs = findBlobs(tFrame);

    // If there exists blobs
    if (blobs.size()) {
      // Calculate target blob
      Blob targetBlob = findLargestBlob(blobs);
      if (receivedTargetBuffer.swapBackIfReady()) {
        // Received target command
        targetDist = sqrt(targetBlob.size);
      }

      // Send all blobs
      vector<Commands::Blob> commandBlobs(blobs.size());
      blobListToCmdBlobVect(commandBlobs, blobs);
      send.sendAutonomyBlobs(commandBlobs);
      send.sendAutonomyBlob(targetBlob.x, targetBlob.y);

      // Calculate Controls
      Control control = calculateFlightControlsPID(targetBlob);
      send.sendAutonomyControl(control);
      arbiter.autonomousControl(control);
    }
  }
}

Autonomy2::Blob::Blob() : x(0), y(0), size(0) {}

inline void Autonomy2::Blob::addPixel(int x, int y) {
  Blob::x += x;
  Blob::y += y;
  size++;
}

void Autonomy2::Blob::calculateCentroid() {
  if (size) {
    x /= size;
    y /= size;
  }
}
