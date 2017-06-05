#include <mpi.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <vector>
#include <algorithm>

using namespace std;

#define S_INITIAL_ELDERS_COUNT 20
#define N_JUDGE 5
#define R_RINGS 5
#define Z_MAX_HEALTH 100
#define D_DOCTORS 5

#define REQUEST 100
#define CONFIRMATION 101
#define EXIT 102

#define SHOW_LOGS

struct message {
    int time;
    int freeDoctors;
    int type;
    int owner;

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

struct elder {
    string id;
    int health;
};

struct official {
    int id;
    vector<elder> elders;
};

struct ring {
    int id;
};

int tid, size;
int freeJudges = N_JUDGE;
int freeDoctors = D_DOCTORS;
int ownTime = 0;
bool initialized = false;
vector <message> queue;
MPI_Status status;
vector<ring> rings;

// BEGIN initializations
void initOfficial() {
    official off;
    off.id = tid;
    for (int i=0; i<S_INITIAL_ELDERS_COUNT; i++) {
        elder newElder;
        newElder.id = to_string(tid) + to_string(i);
        newElder.health = Z_MAX_HEALTH;
        off.elders.push_back(newElder);
    }
}

void initRings() {
    for (int i=0; i<R_RINGS; i++) {
        ring newRing;
        newRing.id = i;
        rings.push_back(newRing);
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

void exitCriticalSection() {
    message request;
    request.type = EXIT;
    request.owner = tid;
    request.time = -1;

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

void init() {
    initOfficial();
    initRings();
}

int main(int argc, char **argv)
{   
    MPI_Init(&argc, &argv);
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &tid );
    // init();
    while(1) {
        if (initialized == false) {
            initialized = true;
            sendRequest();
        }
        usleep( rand()%5000000 );
        receiveRequest();
        if (canEnterCriticalSection()) {
            cout << tid << " is entering critical section\n";
            deleteAllConfirmations();
            exitCriticalSection();
        }
    }
    MPI_Finalize();
    return 0;
}