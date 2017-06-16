#include <mpi.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include <string>

using namespace std;

#define N_FOG_MACHINES 2
#define N_RECORDERS 3
#define N_SHEETS 4
#define N_HOUSES 5

#define NEED_DEVICES 101
#define NEED_DEVICES_CONFIRMATION 102
#define DEVICES_EXIT 103
#define NEED_HOUSE 104
#define NEED_HOUSE_CONFIRMATION 105
#define HOUSES_EXIT 106
#define NEED_RETURN_DEVICES 107
#define NEED_RETURN_DEVICES_CONFIRMATION 108

#define HOUSES 201
#define DEVICES 202

// #define SHOW_LOGS

enum states {
    FREE,
    WAITING_FOR_DEVICES,
    WAITING_FOR_HOUSE,
    HAVE_DEVICES,
    LEFT_HOUSE,
    WAITING_RETURN_DEVICES
};

struct house {
    int id;
    int owner;
    int timeout;
    double timestamp;
};

struct message {
    int time;
    int type;
    int owner;
    int freeRecorders;
    int freeFogMachines;
    int freeSheets;
    house houses[N_HOUSES];

    bool operator<(const message& msg) const
    {
        if (time == msg.time) {
            return owner < msg.owner;
        }
        else {
            return time < msg.time;
        }
    }
};

states state;

int tid, size;
int freeRecorders = N_RECORDERS;
int freeFogMachines = N_FOG_MACHINES;
int freeSheets = N_SHEETS;

bool ownRecorder = false;
bool ownFogMachine = false;
bool ownSheet = false;
int ownedHouseNumber = -1;

int devicesTime = 0;
int housesTime = 0;
vector <message> housesQueue;
vector <message> devicesQueue;
house houses[N_HOUSES];
MPI_Status status;

// BEGIN initializations
void initHouses() {
    for (int i = 0; i < N_HOUSES; i++) {
        house newHouse;
        newHouse.id = i;
        newHouse.owner = -1;
        newHouse.timeout = 0;
        newHouse.timestamp = MPI_Wtime();
        houses[i] = newHouse;
    }
}
// END initializations

// BEGIN handle different response types
void handleDevicesRequest(message response) {
    devicesQueue.push_back(response);
    sort(devicesQueue.begin(), devicesQueue.end());
    message request;
    request.time = devicesTime;
    request.type = NEED_DEVICES_CONFIRMATION;
    request.owner = tid;
    if (response.owner != tid) {

        #ifdef SHOW_LOGS
        cout << tid << " is sending NEED_DEVICES_CONFIRMATION response to processes " << response.owner << "\n";
        #endif

        MPI_Send(&request, sizeof(message), MPI_BYTE, response.owner, NEED_DEVICES_CONFIRMATION, MPI_COMM_WORLD);
    }
}

void handleReturnDevicesRequest(message response) {
    devicesQueue.push_back(response);
    sort(devicesQueue.begin(), devicesQueue.end());
    message request;
    request.time = devicesTime;
    request.type = NEED_RETURN_DEVICES_CONFIRMATION;
    request.owner = tid;
    if (response.owner != tid) {

        #ifdef SHOW_LOGS
        cout << tid << " is sending NEED_RETURN_DEVICES_CONFIRMATION response to processes " << response.owner << "\n";
        #endif

        MPI_Send(&request, sizeof(message), MPI_BYTE, response.owner, NEED_RETURN_DEVICES_CONFIRMATION, MPI_COMM_WORLD);
    }
}

void handleHouseRequest(message response) {
    housesQueue.push_back(response);
    sort(housesQueue.begin(), housesQueue.end());
    message request;
    request.time = devicesTime;
    request.type = NEED_HOUSE_CONFIRMATION;
    request.owner = tid;
    if (response.owner != tid) {

        #ifdef SHOW_LOGS
        cout << tid << " is sending NEED_HOUSE_CONFIRMATION response to processes " << response.owner << "\n";
        #endif

        MPI_Send(&request, sizeof(message), MPI_BYTE, response.owner, NEED_HOUSE_CONFIRMATION, MPI_COMM_WORLD);
    }
}

void handleDevicesConfirmation(message response) {
    devicesQueue.push_back(response);
    sort(devicesQueue.begin(), devicesQueue.end());
}

void handleHouseConfirmation(message response) {
    housesQueue.push_back(response);
    sort(housesQueue.begin(), housesQueue.end());
}

void handleReturnDevicesConfirmation(message response) {
    devicesQueue.push_back(response);
    sort(devicesQueue.begin(), devicesQueue.end());
}


void handleExit(message response) {
    if (response.type == DEVICES_EXIT) {
        freeRecorders = response.freeRecorders;
        freeFogMachines = response.freeFogMachines;
        freeSheets = response.freeSheets;
        for (int i = 0; i < devicesQueue.size(); i++) {
            if((devicesQueue[i].type == NEED_DEVICES || devicesQueue[i].type == NEED_RETURN_DEVICES) && (devicesQueue[i].owner == response.owner)){
                devicesQueue.erase(devicesQueue.begin() + i);
                i--;
            }
        }
    } else if (response.type == HOUSES_EXIT) {
        for (int i = 0; i < N_HOUSES; i++) {
            houses[i] = response.houses[i];
            if (houses[i].timeout > 0) {
                houses[i].timestamp = MPI_Wtime() + houses[i].timeout;
                houses[i].timeout = 0;
            }
        }
        for (int i = 0; i < housesQueue.size(); i++) {
            if((housesQueue[i].type == NEED_HOUSE) && (housesQueue[i].owner == response.owner)){
                housesQueue.erase(housesQueue.begin() + i);
                i--;
            }
        }
    }
}
// END handle different response types

void sendRequest(int type) {
    message request;
    request.time = (type == NEED_HOUSE ? housesTime : devicesTime);
    request.type = type;
    request.owner = tid;

    #ifdef SHOW_LOGS
    cout << tid << " is sending request with type " << type << " to all other processes\n";
    #endif

    for (int i=0; i<size; i++) {
        MPI_Send(&request, sizeof(message), MPI_BYTE, i, type, MPI_COMM_WORLD);
    }
    if (type == NEED_DEVICES) {
        devicesTime++;
        state = WAITING_FOR_DEVICES;
    } else if (type == NEED_RETURN_DEVICES) {
        devicesTime++;
        state = WAITING_RETURN_DEVICES;
    } else if (type == NEED_HOUSE) {
        housesTime++;
        state = WAITING_FOR_HOUSE;
    }
}

// Receive request and call proper handler
void receiveRequest() {
    message response;
    MPI_Recv(&response, sizeof(message), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    if (response.type == NEED_DEVICES || response.type == NEED_RETURN_DEVICES)
        devicesTime = max(response.time, devicesTime) + 1;
    else if (response.type == NEED_HOUSE)
        housesTime = max(response.time, housesTime) + 1;

    #ifdef SHOW_LOGS
    cout << tid << " received request of type " << response.type << " from " << response.owner << " with time " << response.time << "\n";
    #endif

    switch (response.type) {
    case NEED_DEVICES:
        handleDevicesRequest(response);
        break;
    case NEED_HOUSE:
        handleHouseRequest(response);
        break;
    case NEED_DEVICES_CONFIRMATION:
        handleDevicesConfirmation(response);
        break;
    case NEED_HOUSE_CONFIRMATION:
        handleHouseConfirmation(response);
        break;
    case NEED_RETURN_DEVICES:
        handleReturnDevicesRequest(response);
        break;
    case NEED_RETURN_DEVICES_CONFIRMATION:
        handleReturnDevicesConfirmation(response);
        break;
    case DEVICES_EXIT:
    case HOUSES_EXIT:
        handleExit(response);
        break;
    default:
        break;
    }
}

// Exit critical section and inform other processed about devices counts
void exitCriticalSection(int type) {
    message request;
    request.owner = tid;
    request.time = -1;
    if (type == DEVICES) {
        request.type = DEVICES_EXIT;
        request.freeRecorders = freeRecorders;
        request.freeFogMachines = freeFogMachines;
        request.freeSheets = freeSheets;

        #ifdef SHOW_LOGS
        cout << tid << " is sending DEVICES_EXIT request to all proccesses\n";
        #endif

        for (int i = 0; i < size; i++) {
            MPI_Send(&request, sizeof(message), MPI_BYTE, i, DEVICES_EXIT, MPI_COMM_WORLD);
        }
    } else if (type == HOUSES) {
        request.type = HOUSES_EXIT;
        for (int i = 0; i < N_HOUSES; i++) request.houses[i] = houses[i];

        #ifdef SHOW_LOGS
        cout << tid << " is sending HOUSES_EXIT request to all proccesses\n";
        #endif

        for (int i = 0; i < size; i++) {
            MPI_Send(&request, sizeof(message), MPI_BYTE, i, HOUSES_EXIT, MPI_COMM_WORLD);
        }
    }
}

// After entering critical section delete confirmations from all other processes
void deleteAllConfirmations(int type) {
    if (type == DEVICES) {
        for (int i = 0; i < devicesQueue.size(); i++)
            if (devicesQueue[i].type == NEED_DEVICES_CONFIRMATION || devicesQueue[i].type == NEED_RETURN_DEVICES_CONFIRMATION) {
                devicesQueue.erase(devicesQueue.begin() + i);
                i--;
            }
    } else if (type == HOUSES) {
        for (int i = 0; i < housesQueue.size(); i++)
            if (housesQueue[i].type == NEED_HOUSE_CONFIRMATION) {
                housesQueue.erase(housesQueue.begin() + i);
                i--;
            }
    }
}

// Check if process if first in queue
bool isFirst(int type) {
    if (type == DEVICES) {
        for (int i = 0; i < devicesQueue.size(); i++) {
            if (devicesQueue[i].type == NEED_DEVICES || devicesQueue[i].type == NEED_RETURN_DEVICES)
                if (devicesQueue[i].owner == tid) return true; else return false;
        }
    } else if (type == HOUSES) {
        for (int i = 0; i < housesQueue.size(); i++) {
            if (housesQueue[i].type == NEED_HOUSE)
                if (housesQueue[i].owner == tid) return true; else return false;
        }
    }
}

// Check if process has request confirmations from all other processes 
bool hasAllConfirmations(int type) {
    int confirmationCounter = 0;
    int myTimestamp = -1;
    if (type == DEVICES) {
        for (int i = 0; i < devicesQueue.size(); i++)
            if (devicesQueue[i].owner == tid) {
                myTimestamp = devicesQueue[i].time;
                break;
            }

        for (int i = 0; i < devicesQueue.size(); i++)
            if ((devicesQueue[i].type == NEED_DEVICES_CONFIRMATION || devicesQueue[i].type == NEED_RETURN_DEVICES_CONFIRMATION) && devicesQueue[i].time > myTimestamp)
                confirmationCounter++;
    } else if (type == HOUSES) {
        for (int i = 0; i < housesQueue.size(); i++)
            if (housesQueue[i].owner == tid) {
                myTimestamp = housesQueue[i].time;
                break;
            }

        for (int i = 0; i < housesQueue.size(); i++)
            if (housesQueue[i].type == NEED_HOUSE_CONFIRMATION && housesQueue[i].time > myTimestamp)
                confirmationCounter++;
    }
    return (confirmationCounter == (size - 1));
}

// Check if process can enter critical section
bool canEnterCriticalSection(int type) {
    return isFirst(type) && hasAllConfirmations(type);
}

void bookDevices() {
    cout << tid << " booked: ";
    if (freeFogMachines > 0 && !ownFogMachine) {
        freeFogMachines--;
        ownFogMachine = true;
        cout << "fog machine, ";
    }
    if (freeRecorders > 0 && !ownRecorder) {
        freeRecorders--;
        ownRecorder = true;
        cout << "recorder, ";
    }
    if (freeSheets > 0 && !ownSheet) {
        freeSheets--;
        ownSheet = true;
        cout << "sheet, ";
    }
    cout << "freeFogMachines: " << freeFogMachines << ", freeRecorders: " << freeRecorders << ", freeSheets: " << freeSheets << "\n";
}

void returnDevices() {
    ownFogMachine = false;
    ownRecorder = false;
    ownSheet = false;
    freeFogMachines++;
    freeRecorders++;
    freeSheets++;
    cout << tid << " returnedDevices, freeFogMachines: " << freeFogMachines << " freeRecorders: " << freeRecorders << " freeSheets: " << freeSheets << "\n";
}

bool ownAllDevices() {
    return (ownFogMachine && ownRecorder && ownSheet);
}

bool bookHouse() {
    bool foundHouse = false;
    cout << tid << " ";
    for (int i = 0; i < N_HOUSES; i++) {
        cout << "[" << houses[i].timestamp << "] ";
    }
    for (int i = 0; i < N_HOUSES; i++) {
        double currTime = MPI_Wtime();
        cout << " currTime: " << currTime;
        if (houses[i].owner < 0 && currTime > houses[i].timestamp) {
            houses[i].owner = tid;
            ownedHouseNumber = i;
            foundHouse = true;
            cout << tid << ". Booking house number " << houses[i].id << ". Hounting house!\n";
            break;
        }
    }
    if (!foundHouse) cout << tid << " didn't find free house\n";
    return foundHouse;
}

void freeHouse() {
    int timeout = (rand() % 5) + 5;
    for (int i = 0; i < N_HOUSES; i++) {
        if (houses[i].owner == tid) {
            cout << tid << " leaving house number " << houses[i].id << "\n";
            houses[i].owner = -1;
            houses[i].timeout = timeout;
            ownedHouseNumber = -1;
            break;
        }
    }
}

bool hasReservedHouse() {
    return ownedHouseNumber >= 0;
}

void init() {
    state = FREE;
    initHouses();
}

int main(int argc, char **argv)
{   
    MPI_Init(&argc, &argv);
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &tid );
    init();
    while(1) {
        usleep( rand()%500000 );
        if (state == FREE) {
            sendRequest(NEED_DEVICES);
            state = WAITING_FOR_DEVICES;
        } else if (state == HAVE_DEVICES) {
            sendRequest(NEED_HOUSE);
            state = WAITING_FOR_HOUSE;
        } else if (state == LEFT_HOUSE) {
            sendRequest(NEED_RETURN_DEVICES);
            state = WAITING_RETURN_DEVICES;
        }
        receiveRequest();
        if (state == WAITING_FOR_DEVICES && canEnterCriticalSection(DEVICES)) {
            cout << tid << " is entering devices critical section\n";
            bookDevices();
            if (ownAllDevices()) {
                cout << tid << " has all devices\n";
                state = HAVE_DEVICES;
            } else {
                state = FREE;
            }
            deleteAllConfirmations(DEVICES);
            exitCriticalSection(DEVICES);
            cout << tid << " is leaving devices critical section\n\n";
        } else if (state == WAITING_FOR_HOUSE && canEnterCriticalSection(HOUSES)) {
            cout << tid << " is entering houses critical section\n";
            bool success = bookHouse();
            if (success) {
                freeHouse();
                state = LEFT_HOUSE;
            }
            deleteAllConfirmations(HOUSES);
            exitCriticalSection(HOUSES);
            cout << tid << " is leaving houses critical section\n\n";
        } else if (state == WAITING_RETURN_DEVICES && canEnterCriticalSection(DEVICES)) {
            cout << tid << " is entering devices critical section\n";
            returnDevices();
            state = FREE;
            deleteAllConfirmations(DEVICES);
            exitCriticalSection(DEVICES);
            cout << tid << " is leaving devices critical section\n\n";
        }
    }
    MPI_Finalize();
    return 0;
}