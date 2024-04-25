#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <deque>
#include <optional>
#include <stack>
#include <functional>
#include <limits>
#include <iomanip>

using namespace std;


/*
***********************************************************
                    Step 0: Requirenment  
***********************************************************
*/
std::string sched_name;
int verbose_flag = 0;
int trace_flag = 0;
int per_thread_flag = 0;
int enum_flag = 0;
char* schedspec = nullptr;
int quantum = 10001;            // For FCFS, LCFS, SRTF
int maxprio = 4;                // Default value if not specified
char* inputfile = nullptr;
char* randfile = nullptr;
int io_utilization = 0;   
int final_time = -1;            // Final Timestamp
bool PrePrio_flag = false;

enum class State { CREATED, READY, RUNNING, BLOCKED, DONE, PREEMPT };

std::string state_to_string(State state) {
    switch (state) {
        case State::CREATED: return "CREATED";
        case State::READY: return "READY";
        case State::RUNNING: return "RUNNING";
        case State::BLOCKED: return "BLOCKED";
        case State::DONE: return "DONE";
        case State::PREEMPT: return "PREEMPT";
        default: return "UNKNOWN";
    }
}

class RandomNumberGenerator {
private:
    std::ifstream randFile;
    int totalNumbers;
    int currentNumberCount;

public:
    RandomNumberGenerator(const std::string& filename) {
        randFile.open(filename);
        randFile >> totalNumbers;
        currentNumberCount = 0;
    }

    int getNextRandomNumber(int burst) {

        if (currentNumberCount == totalNumbers) { // If at end, rewind
            randFile.clear();                       // Clear EOF flag
            randFile.seekg(0, std::ios::beg);       // Go to the start
            randFile >> totalNumbers;               // Read the count again
            currentNumberCount = 0;
        }

        int num;
        if (!(randFile >> num)) {                   // Attempt to read the next number
            std::cerr << "Failed to read a number from the file." << std::endl;
            exit(EXIT_FAILURE);
        }

        currentNumberCount++;
        return 1 + (num % burst);
    }

    ~RandomNumberGenerator() {
        if (randFile.is_open()) {
            randFile.close();
        }
    }
};


/*
***********************************************************
                    Step 1: Process Objects 
***********************************************************
*/

struct Process {
    int Process_ID;
    int Arrival_Time;
    int Total_CPU_Time;
    int CPU_Burst;
    int IO_Burst;
    int static_prio;
    int dynamic_prio;
    int state_timestamp; // Time when moved into current state
    State process_state;

    // Fields for tracking process execution
    int time_running = 0;
    int time_blocked = 0;
    int finishing_timestamp = 0; // finishing time of a process
    int cpu_waiting_time = 0; // Time in READY state before getting the CPU

    int curr_cpu_burst = 0; // Current CPU burst duration
    int curr_io_burst = 0;  // Current IO burst duration

    // Default Constructor
    Process() : Process_ID(0), Arrival_Time(0), Total_CPU_Time(0), CPU_Burst(0), IO_Burst(0), static_prio(0), dynamic_prio(0), state_timestamp(0), process_state(State::CREATED) {
        // Initializes all members to default values.
    }

    // Constructor with parameters
    Process(int pid, int AT, int TC, int CB, int IO, int stat_prio, State st) :
        Process_ID(pid), Arrival_Time(AT), Total_CPU_Time(TC), CPU_Burst(CB), IO_Burst(IO), static_prio(stat_prio), process_state(st)  {
        state_timestamp = AT;
        reset_dynam_prio();
    }

    // Reset dynamic priority to its static priority minus one.
    void reset_dynam_prio() {
        dynamic_prio = static_prio - 1;
    }

    // Method to print all process details
    std::string get_process_details() const {
        std::ostringstream details;
        details << "Process ID: " << Process_ID
                << ", Arrival Time: " << Arrival_Time
                << ", Total CPU Time: " << Total_CPU_Time
                << ", CPU Burst: " << CPU_Burst
                << ", IO Burst: " << IO_Burst
                << ", Static Priority: " << static_prio
                << ", Dynamic Priority: " << dynamic_prio
                << ", State TimeStamp: " << state_timestamp
                << ", State: " << state_to_string(process_state)
                << ", Time Running: " << time_running
                << ", Time Blocked: " << time_blocked
                << ", Finishing Timestamp: " << finishing_timestamp
                << ", CPU Waiting Time: " << cpu_waiting_time
                << ", Current CPU Burst: " << curr_cpu_burst
                << ", Current IO Burst: " << curr_io_burst;
        return details.str();
    }

    void process_details() const {
        std::cout << get_process_details() << std::endl;
    }

};

Process create_process(int id, int at, int tc, int cb, int io, int stat_prio, State st) {
    return Process(id, at, tc, cb, io, stat_prio, st);
}

std::unordered_map<int, Process> processMap;
std::queue<int> processQueue;

/*
***********************************************************
                    Step 2: Events  
***********************************************************
*/

enum class Trans { TRANS_TO_READY, TRANS_TO_RUN, TRANS_TO_BLOCK, TRANS_TO_PREEMPT };

struct Event {
    int timestamp;
    int processId; // Store process ID
    Trans transition; // Corrected type name to match the enum
    State state;
    bool prempt_flag;

    // Constructor
    Event(int ts, int procId, Trans trans_to, State st, bool pf) :
        timestamp(ts), processId(procId), transition(trans_to), state(st), prempt_flag(pf) {}

    Event() : timestamp(0), processId(-1), transition(Trans::TRANS_TO_BLOCK), state(State::DONE), prempt_flag(false) {}
};

std::deque<Event> eventQueue;

void put_event(std::deque<Event>& eventQueue, const Event& event) {
    auto it = std::upper_bound(eventQueue.begin(), eventQueue.end(), event,
                               [](const Event& a, const Event& b) {
                                   return a.timestamp < b.timestamp;
                               });
    eventQueue.insert(it, event);
}


Event get_event(std::deque<Event>& eventQueue) {
    if (eventQueue.empty()) {
        return Event();
    }

    // Get the first event.
    Event firstEvent = std::move(eventQueue.front());
    eventQueue.pop_front();
    return firstEvent;
}

int get_next_event_time(std::deque<Event>& eventQueue) {
    if (eventQueue.empty()) {
        return -1;
    }

    // Get the first event timestamp
    return eventQueue.front().timestamp;
}

void remove_event(int processId) {
    for(auto it = eventQueue.begin(); it != eventQueue.end(); /* no increment here */) {
        if(it->processId == processId) {
            it = eventQueue.erase(it); // Erase returns the next iterator
        } else {
            ++it; // Only increment if not erasing
        }
    }
}


/*
***********************************************************
                    Step 3: Read Input  
***********************************************************
*/


void read_process(const std::string& filePath, int maxprio, RandomNumberGenerator& rng) {
    std::ifstream inFile(filePath);
    std::string line;
    processMap.clear();
    processQueue = std::queue<int>(); // Reinitialize to clear
    int lineNum = 0;

    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        int at, tc, cb, io;
        if (iss >> at >> tc >> cb >> io) {
            int processId = lineNum; // Assuming lineNum is used as Process_ID
            processMap[processId] = Process(processId, at, tc, cb, io, rng.getNextRandomNumber(maxprio), State::CREATED);
            processQueue.push(processId); // Add Process_ID to the queue
            put_event(eventQueue, Event(at, processId, Trans::TRANS_TO_READY, State::CREATED, false));
        } else {
            std::cerr << "Error parsing line " << lineNum << std::endl;
        }
        lineNum++;
    }
}

/*
***********************************************************
                    Step 4: Schedulers  
***********************************************************
*/

class Scheduler {
public:
    virtual ~Scheduler() {}

    // Adds a process to the scheduler
    virtual void add_process(int processId) = 0;

    // Returns the next process ID from the scheduler. Returns -1 when no processes are available.
    virtual int get_next_process() = 0;

    // Checks if the scheduler has any pending processes
    virtual bool has_processes() const = 0;

    // Get the total number of pending processes in the scheduler
    virtual size_t get_process_count() const = 0;
};


class FCFS_Scheduler : public Scheduler {
private:
    std::queue<int> FCFS_process_queue; // Stores process IDs

public:
    void add_process(int processId) override {
        FCFS_process_queue.push(processId);
    }

    int get_next_process() override {
        if (FCFS_process_queue.empty()) {
            return -1; // Indicate that no process is available
        }
        int nextProcessId = FCFS_process_queue.front();
        FCFS_process_queue.pop();
        return nextProcessId;
    }

    bool has_processes() const override {
        return !FCFS_process_queue.empty();
    }

    size_t get_process_count() const override {
        return FCFS_process_queue.size();
    }
};


class LCFS_Scheduler : public Scheduler {
private:
    std::stack<int> LCFS_process_stack; // Stack to store process IDs

public:
    void add_process(int processId) override {
        LCFS_process_stack.push(processId);
    }

    int get_next_process() override {
        if (LCFS_process_stack.empty()) {
            return -1; // Indicate that no process is available
        }
        int nextProcessId = LCFS_process_stack.top();
        LCFS_process_stack.pop();
        return nextProcessId;
    }

    bool has_processes() const override {
        return !LCFS_process_stack.empty();
    }

    size_t get_process_count() const override {
        return LCFS_process_stack.size();
    }
};

class SRTF_Scheduler : public Scheduler {
private:
    std::deque<int> SRTF_process_deque; // Store only process IDs

public:
    void add_process(int processId) override {
        int remainingTime = get_remaining_time(processId);
        
        // Find the correct position to insert the new process
        auto it = find_insert_position(remainingTime);
        
        // Insert the process ID at the found position
        SRTF_process_deque.insert(it, processId);
    }

    int get_next_process() override {
        if (SRTF_process_deque.empty()) {
            return -1; // Indicate that no process is available
        }
        int nextProcessId = SRTF_process_deque.front();
        SRTF_process_deque.pop_front(); // Remove the process from the deque
        return nextProcessId;
    }

    bool has_processes() const override {
        return !SRTF_process_deque.empty();
    }

    size_t get_process_count() const override {
        return SRTF_process_deque.size();
    }

    // Helper function to find the correct insertion position
    std::deque<int>::iterator find_insert_position(int remainingTime) {
        for (auto it = SRTF_process_deque.begin(); it != SRTF_process_deque.end(); ++it) {
            if (get_remaining_time(*it) > remainingTime) {
                return it; // Found the position where the new process should be inserted
            }
        }
        return SRTF_process_deque.end(); // If not found, insert at the end
    }

    
    static int get_remaining_time(int processId) {
        auto it = processMap.find(processId);
        if (it != processMap.end()) {
            const Process& process = it->second;
            return process.Total_CPU_Time - process.time_running;
        }
        return -1; // Indicate error or not found
    }
};

class RR_Scheduler : public Scheduler{
private:
    std::queue<int> RR_process_queue; // Stores process IDs

public:
    void add_process(int processId) override {
        RR_process_queue.push(processId);
    }

    int get_next_process() override {
        if (RR_process_queue.empty()) {
            return -1; // Indicate that no process is available
        }
        int nextProcessId = RR_process_queue.front();
        RR_process_queue.pop();
        return nextProcessId;
    }

    bool has_processes() const override {
        return !RR_process_queue.empty();
    }

    size_t get_process_count() const override {
        return RR_process_queue.size();
    }
};


class PRIO_Scheduler : public Scheduler {
private:
    vector<queue<int>> ac_Queue, ex_Queue;
    int num_active, num_expired;

public:
    PRIO_Scheduler() : num_active(0), num_expired(0) {
        ac_Queue.resize(maxprio);
        ex_Queue.resize(maxprio);
    }

    void add_process(int processId) override {
        Process* process = &processMap[processId]; 

        if(process->process_state == State::PREEMPT) {
            process->dynamic_prio--;
        }

        process->process_state = State::READY; // Update process state to READY

        if(process->dynamic_prio < 0) {
            process->dynamic_prio = process->static_prio - 1;
            ex_Queue[process->dynamic_prio].push(processId);
            num_expired++;
        } else {
            ac_Queue[process->dynamic_prio].push(processId);
            num_active++;
        }
    }

    int get_next_process() override {
        if(num_active == 0) {
            // Swap Active with Expired
            std::swap(ac_Queue, ex_Queue);
            num_active = num_expired;
            num_expired = 0;
        }

        for(int i = maxprio - 1; i >= 0; i--) {
            if(!ac_Queue[i].empty()) {
                int processId = ac_Queue[i].front();
                ac_Queue[i].pop();
                num_active--;
                return processId;
            }
        }

        return -1; // Indicate that no process is ready
    }

    bool has_processes() const override {
        return num_active > 0 || num_expired > 0;
    }

    size_t get_process_count() const override {
        return static_cast<size_t>(num_active + num_expired);
    }


};


class PREPRIO_Scheduler : public Scheduler{
private:
    vector<queue<int>> ac_Queue, ex_Queue;
    int num_active, num_expired;

public:
    PREPRIO_Scheduler() : num_active(0), num_expired(0) {
        ac_Queue.resize(maxprio);
        ex_Queue.resize(maxprio);
    }

    void add_process(int processId) override {
        Process* process = &processMap[processId]; 

        if(process->process_state == State::PREEMPT) {
            process->dynamic_prio--;
        }

        process->process_state = State::READY; // Update process state to READY

        if(process->dynamic_prio < 0) {
            process->dynamic_prio = process->static_prio - 1;
            ex_Queue[process->dynamic_prio].push(processId);
            num_expired++;
        } else {
            ac_Queue[process->dynamic_prio].push(processId);
            num_active++;
        }
    }

    int get_next_process() override {
        if(num_active == 0) {
            // Swap Active with Expired
            std::swap(ac_Queue, ex_Queue);
            num_active = num_expired;
            num_expired = 0;
        }

        for(int i = maxprio - 1; i >= 0; i--) {
            if(!ac_Queue[i].empty()) {
                int processId = ac_Queue[i].front();
                ac_Queue[i].pop();
                num_active--;
                return processId;
            }
        }

        return -1; // Indicate that no process is ready
    }

    bool has_processes() const override {
        return num_active > 0 || num_expired > 0;
    }

    size_t get_process_count() const override {
        return static_cast<size_t>(num_active + num_expired);
    }
};

bool isPriorityLess(const int Process_Id, const int running_process_ID) {
    return processMap[running_process_ID].dynamic_prio < processMap[Process_Id].dynamic_prio;
}


Scheduler* my_scheduler = nullptr; // Pointer to the base class

/*
***********************************************************
                    Step 5: DES  
***********************************************************
*/

void print_summary();

void Simulation(std::deque<Event>& eventQueue, RandomNumberGenerator& rng, Scheduler* my_scheduler){
    Event curr_event;
    int Process_Id;
    Process curr_process;
    int curr_time = 0;
    Trans transition;
    State curr_state;
    int previous_state_time;
    int cpu_burst;

    Trans new_transition;
    int event_time;
    int time_remaining;
    int runn_process_time;

    bool CALL_SCHEDULER = false;
    int new_process_id = -1;
    bool process_running = false;
    int running_process_ID;

    int io_concurrency = 0;     // Number of process doing I/O concurrently 
    int io_working = 0;         // timestamp of the latest occurence when io_count goes from 0 to 1

    while (!eventQueue.empty()) {
        curr_event = get_event(eventQueue);              // Event 
        if(curr_event.processId == -1){
            break;
        }

        Process_Id = curr_event.processId;               // Process the event works on
        curr_process = processMap[Process_Id];
        curr_time = curr_event.timestamp;
        transition = curr_event.transition;
        curr_state = curr_process.process_state;        
        
        if (verbose_flag == 1){
            previous_state_time = curr_time - curr_process.state_timestamp; 
            std::cout << curr_time << " " << Process_Id << " " << previous_state_time <<": ";
        }

        switch (transition) { // Encodes where we come from and where we go
            case Trans::TRANS_TO_READY:{
                switch(curr_state){
                    case State::CREATED:
                        curr_process.state_timestamp = curr_time;
                        curr_process.process_state = State::READY;
                        processMap[Process_Id] = curr_process;

                        if (verbose_flag == 1){
                            std::cout << "CREATED -> READY" << std::endl;
                        }
                        break;

                    case State::BLOCKED:
                        previous_state_time = curr_time - curr_process.state_timestamp; 
                        curr_process.time_blocked += previous_state_time;
                        curr_process.state_timestamp = curr_time;
                        curr_process.reset_dynam_prio();
                        curr_process.curr_io_burst = 0;
                        curr_process.process_state = State::READY;
                        processMap[Process_Id] = curr_process;

                        if (verbose_flag == 1){
                            std::cout << "BLOCK -> READY" << std::endl;
                        }

                        io_concurrency--;
                        if (io_concurrency == 0)
                            io_utilization += curr_time - io_working;

                        break;
                    
                    default:
                        // Error Message 
                        exit(1);
                }
                
                // For PrePRIO               
                
                if(PrePrio_flag && process_running == true){
                    // runn_process = processMap[running_process_ID];
                    
                    for (std::deque<Event>::iterator it = eventQueue.begin(); it != eventQueue.end(); ++it) {
                        Event& event = *it;
                        if(event.processId == running_process_ID){
                            runn_process_time = event.timestamp;
                        }
                    }
                    
                    if(curr_process.dynamic_prio > processMap[running_process_ID].dynamic_prio && curr_time < runn_process_time){
                        
                        remove_event(running_process_ID);

                        Event new_event = Event(curr_time, running_process_ID, Trans::TRANS_TO_PREEMPT, State::RUNNING, false);
                        put_event(eventQueue, new_event);  
                    }

                    if (verbose_flag == 1){
                        bool condition_1 = isPriorityLess(Process_Id, running_process_ID);
                        bool condition_2 = curr_time < runn_process_time;

                        string do_prempt;
                        if (condition_1 && condition_2) {
                            do_prempt = "YES";
                        } else {
                            do_prempt = "NO";
                        }
                        std::cout << "    --> PrioPreempt ";
                        printf("Cond1=%d Cond2=%d (%d) --> %s\n", condition_1, condition_2, Process_Id, do_prempt.c_str());
                    }

                }            

                
                my_scheduler->add_process(Process_Id);
                CALL_SCHEDULER = true;
                break;
            }

            case Trans::TRANS_TO_PREEMPT:{                                    

                previous_state_time = curr_time - curr_process.state_timestamp; 

                curr_process.time_running += previous_state_time;
                curr_process.curr_cpu_burst -= previous_state_time;
                curr_process.state_timestamp = curr_time;
                curr_process.process_state = State::PREEMPT;
                process_running = false;
                processMap[Process_Id] = curr_process;
                my_scheduler->add_process(Process_Id);
                
                time_remaining = curr_process.Total_CPU_Time - curr_process.time_running;

                if (verbose_flag == 1){
                    std::cout << "RUNNG -> READY  cb=" << curr_process.curr_cpu_burst << " rem=" << time_remaining << " prio=" << curr_process.dynamic_prio << std::endl;
                }

                // HasPrio for P and E

                CALL_SCHEDULER = true;
                break;
            }

            case Trans::TRANS_TO_RUN:{
                int new_cpu_burst;
                if (curr_process.curr_cpu_burst > 0) {
                    new_cpu_burst = curr_process.curr_cpu_burst;
                } else {
                    new_cpu_burst = rng.getNextRandomNumber(curr_process.CPU_Burst);
                }

                previous_state_time = curr_time - curr_process.state_timestamp; 
                curr_process.cpu_waiting_time += previous_state_time;
                curr_process.state_timestamp = curr_time;

                time_remaining = curr_process.Total_CPU_Time - curr_process.time_running;

                curr_process.curr_cpu_burst =  new_cpu_burst > time_remaining ? time_remaining : new_cpu_burst;
                // curr_process.dynamic_prio--;
                curr_process.process_state = State::RUNNING;
                processMap[Process_Id] = curr_process;

                // state - ready 
                Event new_event;
                if (quantum >= curr_process.curr_cpu_burst) {
                    event_time = curr_time + curr_process.curr_cpu_burst;
                    new_transition = Trans::TRANS_TO_BLOCK;
                } else {
                    event_time = curr_time + quantum;
                    new_transition = Trans::TRANS_TO_PREEMPT;
                }
                new_event = Event(event_time, Process_Id, new_transition, State::RUNNING, false);
                put_event(eventQueue, new_event); 

                if (verbose_flag == 1){
                    std::cout << "READY -> RUNNG cb=" << curr_process.curr_cpu_burst << " rem=" << time_remaining << " prio=" << curr_process.dynamic_prio << std::endl;
                }

                CALL_SCHEDULER = true;
                break;
            }

            case Trans::TRANS_TO_BLOCK:{

                previous_state_time = curr_time - curr_process.state_timestamp; 
                curr_process.time_running += previous_state_time;
                curr_process.curr_cpu_burst -= previous_state_time;
                
                if (curr_process.time_running == curr_process.Total_CPU_Time){      
                    curr_process.state_timestamp = curr_time;
                    curr_process.finishing_timestamp = curr_time;
                    processMap[Process_Id] = curr_process;

                    
                    if (curr_time > final_time){
                        final_time = curr_time;
                    }

                    if (verbose_flag == 1){
                        std::cout << "Done" << std::endl;
                    }
                }
                else{
                    io_concurrency++;
                    if(io_concurrency == 1){
                        io_working = curr_time;
                    }

                    int new_io_burst = rng.getNextRandomNumber(curr_process.IO_Burst);
                    curr_process.state_timestamp = curr_time;
                    curr_process.curr_io_burst = new_io_burst;
                    curr_process.process_state = State::BLOCKED;
                    processMap[Process_Id] = curr_process;
                    event_time = curr_time + new_io_burst;

                    Event new_event = Event(event_time, Process_Id, Trans::TRANS_TO_READY, State::BLOCKED, false);
                    put_event(eventQueue, new_event);           

                    if (verbose_flag == 1){
                        std::cout << "RUNNG -> BLOCK  ib="<< new_io_burst <<" rem="<< curr_process.Total_CPU_Time - curr_process.time_running << std::endl;
                    }
                }

                process_running = false;
                CALL_SCHEDULER = true;
                break;
            }
        }
        if (CALL_SCHEDULER) {
            if (!eventQueue.empty() && get_next_event_time(eventQueue) == curr_time){
                continue; // Process next event from Event queue
            }

            CALL_SCHEDULER = false; // Reset global flag
            if (process_running == false) {
                if(my_scheduler->has_processes()){
                    new_process_id = my_scheduler->get_next_process();
                    Event new_event = Event(curr_time, new_process_id, Trans::TRANS_TO_RUN, State::READY, false);
                    put_event(eventQueue, new_event);         
                    process_running = true;
                    running_process_ID = new_process_id;
                }
                else{
                    continue;
                }
            } 
        } 
        
    } 
    print_summary();
}


/*
***********************************************************
                    Step 6: Print Summary  
***********************************************************
*/


void print_summary() {
    if (quantum < 10000)
        std::cout << sched_name << " " << quantum << "\n";
    else
        std::cout << sched_name << "\n";

    int process_turnaround_time;

    int total_cpu_time = 0;             // Sum of time CPU busy
    int total_weight_time = 0;          // Sum of time in ready state
    int total_turnaround_time = 0;       // Sum of turnaround time
    int process_count = processMap.size();

    while (!processQueue.empty()) {
        int pid = processQueue.front();
        processQueue.pop();
        const Process& curr_process = processMap[pid];

        process_turnaround_time = curr_process.finishing_timestamp - curr_process.Arrival_Time;

        total_turnaround_time += process_turnaround_time;
        total_weight_time += curr_process.cpu_waiting_time;
        total_cpu_time += curr_process.time_running;

        std::cout << std::setw(4) << std::setfill('0') << curr_process.Process_ID << std::setfill(' ') 
                << ":" << std::setw(5) << curr_process.Arrival_Time
                << " " << std::setw(4) << curr_process.Total_CPU_Time
                << " " << std::setw(4) << curr_process.CPU_Burst
                << " " << std::setw(4) << curr_process.IO_Burst
                << " " << std::setw(1) << curr_process.static_prio 
                << " | " << std::setw(5) << curr_process.finishing_timestamp
                << " " << std::setw(5) << process_turnaround_time
                << " " << std::setw(5) << curr_process.time_blocked
                << " " << std::setw(5) << curr_process.cpu_waiting_time << "\n";

    }

    // double total_time = static_cast<double>(final_time);
    double cpu_util = 100.0 * (total_cpu_time / (double) final_time);
    double io_util = 100.0 * (io_utilization / (double) final_time);
    double throughput = 100.0 * (process_count / (double) final_time);
    double avg_wtg = (double) total_weight_time / process_count;
    double avg_trt = (double) total_turnaround_time / process_count;

    std::cout << "SUM: " << final_time << " "
              << std::fixed << std::setprecision(2) << cpu_util << " "
              << io_util << " " << avg_trt << " " << avg_wtg << " "
              << std::setprecision(3) << throughput << "\n";
}


/*
***********************************************************
                    Step 7: Main Funciton 
***********************************************************
*/

int main(int argc, char* argv[]) {

    int option;
    while ((option = getopt(argc, argv, "vteps:")) != -1) {
        switch (option) {
            case 'v':
                verbose_flag = 1;
                break;
            case 't':
                trace_flag = 1;
                break;
            case 'e':
                enum_flag = 1;
                break;
            case 'p':
                per_thread_flag = 1;
                break;
            case 's':
                schedspec = optarg;
                // Parse scheduler specification
                if (schedspec[0] == 'R' && sscanf(schedspec + 1, "%d", &quantum) == 1) {
                    // Handle Round Robin with quantum
                    sched_name = "RR";
                    my_scheduler = new RR_Scheduler();
                } 
                else if (schedspec[0] == 'P' && (sscanf(schedspec + 1, "%d:%d", &quantum, &maxprio) == 2 || sscanf(schedspec + 1, "%d", &quantum) == 1)) {
                    // Handle Priority with quantum and optional maxprio
                    sched_name = "PRIO";
                    my_scheduler = new PRIO_Scheduler();
                } 
                else if (schedspec[0] == 'E' && (sscanf(schedspec + 1, "%d:%d", &quantum, &maxprio) == 2 || sscanf(schedspec + 1, "%d", &quantum) == 1)) {
                    // Handle Preemptive Priority with quantum and optional maxprio
                    sched_name = "PREPRIO";
                    PrePrio_flag = true;
                    my_scheduler = new PREPRIO_Scheduler();
                } 
                else if (strcmp(schedspec, "F") == 0) {
                    // Handle FCFS scheduler 
                    sched_name = "FCFS";
                    my_scheduler = new FCFS_Scheduler();
                } 
                else if (strcmp(schedspec, "L") == 0) {
                    // Handle LCFS scheduler
                    sched_name = "LCFS";
                    my_scheduler = new LCFS_Scheduler();
                } 
                else if (strcmp(schedspec, "S") == 0) {
                    // Handle SRTF scheduler 
                    sched_name = "SRTF";
                    my_scheduler = new SRTF_Scheduler();
                } 
                else {
                    std::cerr << "Invalid scheduler specification: " << schedspec << std::endl;
                    exit(EXIT_FAILURE);
                }
                break;

            case '?':
                // getopt_long already printed an error message
                break;
            default:
                abort();
        }
    }

    // Remaining command line arguments not parsed by getopt_long.
    if (optind < argc) {
        inputfile = argv[optind++];
        randfile = argv[optind];
    }

    RandomNumberGenerator rng(randfile);
    read_process(inputfile, maxprio, rng);

    Simulation(eventQueue, rng, my_scheduler);

    return 0;
}
