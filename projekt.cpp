#include <mpi.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <vector>

#define S_INITIAL_ELDERS_COUNT 20
#define N_JUDGE 5
#define R_RINGS 5
#define Z_MAX_HEALTH 100
#define D_DOCTORS 5

int tid, size;
int freeJudges = N_JUDGE;
int freeDoctors = D_DOCTORS;
MPI_Status status;

struct message {
    int freeDoctors;
};

struct elder {
    std::string id;
    int health;
};

struct official {
    int id;
    std::vector<elder> elders;
};

struct ring {
    int id;
};

std::vector<ring> rings;

void initOfficial() {
    official off;
    off.id = tid;
    for (int i=0; i<S_INITIAL_ELDERS_COUNT; i++) {
        elder newElder;
        newElder.id = std::to_string(tid) + std::to_string(i);
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

void sendRequest() {
    message msg;
    freeDoctors--;
    msg.freeDoctors = freeDoctors;
    std::cout << "proces " << tid << " wysyła " << freeDoctors << " doktorow\n";
    for (int i=0; i<size; i++) {
        if (i != tid) {
            MPI_Send(&msg, sizeof(message), MPI_BYTE, i, 1, MPI_COMM_WORLD);
        }
    }
}

void receiveRequest() {
    message resp;
    MPI_Recv(&resp, sizeof(message), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    freeDoctors = resp.freeDoctors;
    std::cout << "proces " << tid << " odebrał " << freeDoctors << " doktorow\n";
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
    init();
    while(freeDoctors > 0) {
        usleep( rand()%50000 );
        sendRequest();
        receiveRequest();
    }
    MPI_Finalize();
    return 0;
}