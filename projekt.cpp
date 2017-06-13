#include <mpi.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <vector>
#include <algorithm>

using namespace std;

#define N_FOG_MACHINES 2
#define N_RECORDERS 3
#define N_SHEETS 4
#define N_HOUSES 5

#define REQUEST 100
#define CONFIRMATION 101
#define EXIT 102

// #define SHOW_LOGS

enum states {
    FREE,
    WAITING_FOR_RESPONSE
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
    house housesQueue[N_HOUSES];

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

int ownTime = 0;
vector <message> queue;
house housesQueue[N_HOUSES];
MPI_Status status;

// BEGIN initializations
void initHouses() {
    for (int i = 0; i < N_HOUSES; i++) {
        house newHouse;
        newHouse.id = i;
        newHouse.owner = -1;
        newHouse.timeout = 0;
        newHouse.timestamp = MPI_Wtime();
        housesQueue[i] = newHouse;
    }
}
// END initializations

// BEGIN handle different response types
void handleRequest(message response) {
    queue.push_back(response);
    sort(queue.begin(), queue.end());
    message request;
    request.time = ownTime;
    request.type = CONFIRMATION;
    request.owner = tid;
    if (response.owner != tid) {

        #ifdef SHOW_LOGS
        cout << tid << " is sending CONFIRMATION response to processes " << response.owner << "\n";
        #endif

        MPI_Send(&request, sizeof(message), MPI_BYTE, response.owner, CONFIRMATION, MPI_COMM_WORLD);
    }
}

void handleConfirmation(message response) {
    queue.push_back(response);
    sort(queue.begin(), queue.end());
}

void handleExit(message response) {
    for (int i = 0; i < queue.size(); i++) {
        if((queue[i].type == REQUEST) && (queue[i].owner == response.owner)){
            queue.erase(queue.begin() + i);
            i--;
        }
    }
    freeRecorders = response.freeRecorders;
    freeFogMachines = response.freeFogMachines;
    freeSheets = response.freeSheets;
    for (int i = 0; i < N_HOUSES; i++) {
        housesQueue[i] = response.housesQueue[i];
        if (housesQueue[i].timeout > 0) {
            housesQueue[i].timestamp = MPI_Wtime() + housesQueue[i].timeout;
            housesQueue[i].timeout = 0;
        }
    }
}
// END handle different response types

void sendRequest() {
    message request;
    request.time = ownTime;
    request.type = REQUEST;
    request.owner = tid;

    #ifdef SHOW_LOGS
    cout << tid << " is sending request to all other processes\n";
    #endif

    for (int i=0; i<size; i++) {
        MPI_Send(&request, sizeof(message), MPI_BYTE, i, REQUEST, MPI_COMM_WORLD);
    }
    ownTime++;
    state = WAITING_FOR_RESPONSE;
}

// Receive request and call proper handler
void receiveRequest() {
    message response;
    MPI_Recv(&response, sizeof(message), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    ownTime = max(response.time, ownTime) + 1;

    #ifdef SHOW_LOGS
    cout << tid << " received request of type " << response.type << " from " << response.owner << " with time " << response.time << "\n";
    #endif

    switch (response.type) {
    case REQUEST:
        handleRequest(response);
        break;
    case CONFIRMATION:
        handleConfirmation(response);
        break;
    case EXIT:
        handleExit(response);
        break;
    default:
        break;
    }
}

// Exit critical section and inform other processed about devices counts
void exitCriticalSection() {
    message request;
    request.type = EXIT;
    request.owner = tid;
    request.time = -1;
    request.freeRecorders = freeRecorders;
    request.freeFogMachines = freeFogMachines;
    request.freeSheets = freeSheets;
    for (int i = 0; i < N_HOUSES; i++) request.housesQueue[i] = housesQueue[i];

    #ifdef SHOW_LOGS
    cout << tid << " is sending EXIT request to all proccesses\n";
    #endif

    for (int i = 0; i < size; i++) {
         MPI_Send(&request, sizeof(message), MPI_BYTE, i, EXIT, MPI_COMM_WORLD);
    }
}

// After entering critical section delete confirmations from all other processes
void deleteAllConfirmations () {
    for (int i = 0; i < queue.size(); i++)
        if (queue[i].type == CONFIRMATION) {
            queue.erase(queue.begin() + i);
            i--;
        }
}

// Check if process if first in queue
bool isFirst() {
    for (int i = 0; i < queue.size(); i++) {
        if (queue[i].type == REQUEST)
            if (queue[i].owner == tid) return true; else return false;
    }
}

// Check if process has request confirmations from all other processes 
bool hasAllConfirmations() {
    int myTimestamp = -1;
    for (int i = 0; i < queue.size(); i++)
        if (queue[i].owner == tid) {
            myTimestamp = queue[i].time;
            break;
        }

    int confirmationCounter = 0;
    for (int i = 0; i < queue.size(); i++)
        if (queue[i].type == CONFIRMATION && queue[i].time > myTimestamp)
            confirmationCounter++;
    
    return (confirmationCounter == (size - 1));
}

// Check if process can enter critical section
bool canEnterCriticalSection() {
    return isFirst() && hasAllConfirmations();
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
        cout << "[" << housesQueue[i].timestamp << "] ";
    }
    for (int i = 0; i < N_HOUSES; i++) {
        double currTime = MPI_Wtime();
        cout << " currTime: " << currTime;
        if (housesQueue[i].owner < 0 && currTime > housesQueue[i].timestamp) {
            housesQueue[i].owner = tid;
            ownedHouseNumber = i;
            foundHouse = true;
            cout << tid << ". Booking house number " << housesQueue[i].id << ". Hounting house!\n";
            break;
        }
    }
    if (!foundHouse) cout << tid << " didn't find free house\n";
    return foundHouse;
}

void freeHouse() {
    int timeout = (rand() % 15) + 10;
    for (int i = 0; i < N_HOUSES; i++) {
        if (housesQueue[i].owner == tid) {
            cout << tid << " leaving house number " << housesQueue[i].id << "\n";
            housesQueue[i].owner = -1;
            housesQueue[i].timeout = timeout;
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
            sendRequest();
            state = WAITING_FOR_RESPONSE;
        }
        receiveRequest();
        if (canEnterCriticalSection()) {
            cout << tid << " is entering critical section\n";

            if (!ownAllDevices()) {
                bookDevices();
            } else if (!hasReservedHouse()) {
                cout << tid << " has all devices, trying to book house\n";
                bool success = bookHouse();
                if (success) {
                    freeHouse();
                    returnDevices();
                }
            }
            state = FREE;
            deleteAllConfirmations();
            exitCriticalSection();
            cout << tid << " is leaving critical section\n\n";
        }
    }
    MPI_Finalize();
    return 0;
}