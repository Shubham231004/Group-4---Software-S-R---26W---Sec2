# Aircraft–Ground Control Communication System

## Overview

This project implements a distributed client–server communication system that simulates interaction between an aircraft and a ground control station. The system is designed with a strong focus on reliability, structured communication, and traceability, reflecting concepts used in safety-oriented software systems.

The application consists of two main components:

* A **server application** representing ground control
* A **client application** representing an aircraft

The client connects to the server, sends telemetry data, responds to commands, and participates in large data transfer operations. The server validates incoming data, controls the communication flow, and manages the session using a state-based architecture.

---

## Key Features

* Structured TCP-based client–server communication using Winsock
* Custom communication protocol with packet headers and payloads
* Server-side state machine for controlled execution
* Telemetry data exchange with acknowledgements
* Command and response handling
* Large data transfer (>1 MB) with chunking and validation
* Logging system for traceability (`client_log.csv`, `server_log.csv`)
* Comprehensive testing using MSTest
* Static code analysis using PVS-Studio (MISRA-based warnings)
* GitHub-based issue tracking and traceability

---

## Project Structure

```
AircraftGroundControl/
│
├── src/                    # Source files
│   ├── client_main.cpp
│   ├── server_main.cpp
│   ├── client_logic.cpp
│   ├── server_logic.cpp
│   ├── common.cpp
│
├── include/                # Header files
│   ├── client_logic.hpp
│   ├── server_logic.hpp
│   ├── common.hpp
│
├── tests/                  # MSTest projects
│   ├── ClientTests/
│   ├── ServerTests/
│   ├── CommonTests/
│   ├── IntegrationTests/
│
├── build/                  # Build output (generated)
├── CMakeLists.txt          # Build configuration
├── Project Test Log.xlsx   # Test definitions and execution log
└── README.md
```

---

## Technologies Used

* C++ (Core implementation)
* Winsock (TCP networking)
* CMake (Build system)
* Visual Studio 2022
* MSTest (Testing framework)
* PVS-Studio (Static analysis)

---

## Build Instructions

### Prerequisites

Make sure the following are installed:

* Visual Studio 2022 (with C++ development tools)
* CMake
* Windows OS (required for Winsock)

---

### Step 1: Clone the Repository

```
git clone https://github.com/Shubham231004/Group-4---Software-S-R---26W---Sec2.git
cd Group-4---Software-S-R---26W---Sec2
```

---

### Step 2: Configure the Build

```
cmake -S . -B build
```

---

### Step 3: Build the Project

```
cmake --build build
```

After a successful build, executables will be generated inside the `build` directory.

---

## Running the Application

### Step 1: Run the Server

```
build\Debug\server.exe
```

The server will start listening for incoming connections.

---

### Step 2: Run the Client (in a new terminal)

```
build\Debug\client.exe
```

The client will connect to the server and begin communication.

---

## System Workflow

1. Client sends connection request
2. Server verifies and acknowledges
3. Client sends telemetry data periodically
4. Server acknowledges telemetry
5. Server triggers commands and requests
6. Client responds accordingly
7. Large data transfer is initiated
8. Client reconstructs received data
9. Session ends with controlled disconnect

---

## Generated Output

During execution, the system generates:

* `client_log.csv` — client-side communication log
* `server_log.csv` — server-side communication log
* `received_diagnostic_payload.bin` — received large data file

These files are useful for debugging and validation.

---

## Testing

Testing is implemented using MSTest and divided into:

* Client Tests
* Server Tests
* Common Tests
* Integration & System Tests

### Running Tests

Open the solution in Visual Studio and use:

```
Test Explorer → Run All Tests
```

### Test Coverage

* Unit testing for individual components
* Integration testing for client-server communication
* System testing for full workflow
* Usability testing for execution and logs

All tests pass after final fixes.

---

## Static Analysis (MISRA)

Static analysis was performed using PVS-Studio.

* Total MISRA-related warnings: 69
* Standard used: MISRA C++ (as supported by tool)
* Selected warnings were fixed
* Remaining warnings documented as deviations

The analysis improves code quality and highlights safety-oriented practices.

---

## GitHub Traceability

The project uses GitHub for:

* Version control
* Issue tracking
* Bug management
* Collaboration

Each defect identified during testing was:

1. Logged as an issue
2. Investigated
3. Fixed
4. Verified
5. Closed with explanation

This ensures full traceability from testing to resolution.

---

## How to Reproduce the Project

1. Clone the repository
2. Build using CMake
3. Run server first
4. Run client
5. Observe logs and output files
6. Run tests in Visual Studio

---

## Limitations

* Not fully MISRA compliant (partial compliance achieved)
* Windows-only (due to Winsock dependency)
* Designed for academic simulation, not real aerospace deployment

---

## Future Improvements

* Full MISRA compliance
* Cross-platform support
* Enhanced error handling
* Performance optimization for large transfers

---

## Contributors

* Shubham Patel
* Brian
* Yinus

---

## Repository Link

https://github.com/Shubham231004/Group-4---Software-S-R---26W---Sec2

---

## Summary

This project demonstrates a complete software development lifecycle including design, implementation, testing, and compliance analysis. It reflects structured engineering practices and provides a strong foundation for understanding safety and reliability in distributed systems.
