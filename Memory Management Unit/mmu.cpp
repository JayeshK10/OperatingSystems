//  cd "/mnt/d/NYU/Academics/Sem 2/OS/Lab/Lab3"
// ./runit.sh ./myout ./mmu
//  ./gradeit.sh ./refout ./myout  ./LOG.txt
//  ./mmu -f16 -af -oOPSF ./test/in1 ./test/rfile

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <optional>
#include <unistd.h>
#include <map>
#include <queue>
#include <climits>

using namespace std;

/*
*********************************************************************
                    Step 0: Global Variables and Random Input
*********************************************************************
*/

char* inputfile = NULL;      // To hold the input file path
char* randfile = NULL;       // To hold the random file path

// Argument Options
bool option_O_flag = false;
bool option_P_flag = false;
bool option_S_flag = false;
bool option_F_flag = false;

// Constant Variables
const int PTE_max_size = 64;
const int Frame_max_size = 128;

// Other
int num_INSTR = 0;
int num_frames = -1;
int num_contxt_switch = 0;
int num_proc_exit = 0;
long final_cost = 0;
long inst_num = -1;


// OPerations Cost
const int cost_of_read_write = 1;
const int cost_of_context_switches = 130;
const int cost_of_process_exit = 1230;
const int cost_of_maps = 350;
const int cost_of_unmaps = 410;
const int cost_of_ins = 3200;
const int cost_of_outs = 2750;
const int cost_of_fins = 2350;
const int cost_of_fouts = 2800;
const int cost_of_zeros = 150;
const int cost_of_segv = 440;
const int cost_of_segprot = 410;


int total_random_num;
int* random_nums;           // Array to store all random numbers
int ofs = 0;  

void random_stream(const std::string &randfile){
    std::ifstream random_file(randfile);
    if (!random_file) {
        std::cerr << "Error opening file." << std::endl;
        return;
    }

    random_file >> total_random_num;  // Read the total count of random numbers from the first line

    random_nums = new int[total_random_num];
    for (int i = 0; i < total_random_num; i++) {
        if (!(random_file >> random_nums[i])) {
            break;  // Break on reading error or end of file
        }
    }
    random_file.close();  // Close the file after reading
}

int get_random_number() {
    int randomVal = random_nums[ofs] % num_frames;
    ofs = (ofs + 1) % total_random_num;
    return randomVal;
}

/*
*********************************************************************
                    Step 1: Process
*********************************************************************
*/

// VAMs
struct VMA_dict{
    int VMA_ID;
    int VP_start;
    int VP_end;
    int write_protected;                // [0 / 1]
    int filemapped;                     // [0 / 1]

    VMA_dict(int id, int start, int end, int write, int file): 
        VMA_ID(id), VP_start(start), VP_end(end), write_protected(write), filemapped(file){}

    void show_VMA(){
        std::cout << VMA_ID << ": start = " << VP_start << ", end = " << VP_end << ", write_protected = " << write_protected << ", filemapped = " << filemapped << std::endl; 
    }
};

// Virtual Page Table entries
struct PT_entry{
    unsigned int valid : 1;         // Present virtual page 
    unsigned int referenced : 1;    // reference bit
    unsigned int modified : 1;      // modified bit 
    unsigned int write_protect : 1; // write protect for security 
    unsigned int paged_out : 1;     // paged out 
    unsigned int physcial_Addr : 7; // address in physical memory 
    unsigned int file_mapped : 1;   // if file is mapped to a addr in physical memory 
    unsigned int unused : 19;       // extra unused bits

    PT_entry() : valid(0), referenced(0), modified(0), write_protect(0), paged_out(0), physcial_Addr(0), file_mapped(0), unused(0) {}

};
    

std::map<std::string, int> initializeProcessStats(const std::vector<std::string>& keys) {
    std::map<std::string, int> stats;
    for (const auto& key : keys) {
        stats[key] = 0;
    }
    return stats;
}

// Process
class Process{
public:
    int Process_ID;
    int num_VMA;
    std::vector<VMA_dict> vma_list; // Vector of VMA_dict objects
    PT_entry virtual_page_table[PTE_max_size];

    std::map<std::string, int> process_stats = initializeProcessStats({"maps", "unmaps", "ins", "outs", "fins", "fouts", "zeros", "segvs", "segprots"});

    // Constructor
    Process(int id, int vma): Process_ID(id), num_VMA(vma) {} // Initialize with Process ID

    // Add a VMA to the process
    void add_VMA(const VMA_dict& vma){
        this->vma_list.push_back(vma);
        // this->num_VMA += 1;                               // Update the count of VMAs
    }

    // Display Process 
    void show_Process() {
        std::cout << "Process ID: " << Process_ID << std::endl;
        std::cout << "Number of VMAs for the current process: " << num_VMA << std::endl;
        for (auto& vma : vma_list) {
            vma.show_VMA();
        }
    }

    void initialize_PTE(){
        for (int i = 0; i < PTE_max_size; i++) {
            virtual_page_table[i] = PT_entry(); 
        }
    }
};

/*
*********************************************************************
                    Step 2: Read Input
*********************************************************************
*/

// Instructions
struct Instructions{
    int inst_ID;
    char opr;
    int v_page;                 

    Instructions(int id, char ch, int vp): 
        inst_ID(id), opr(ch), v_page(vp){}

    void show_Inst(){
        std::cout << inst_ID << ": ==> " << opr << " " << v_page << std::endl; 
    }
};

vector<Process*> process_list;
queue<Instructions> Instr_table;

void read_input(const std::string& filePath){
    std::ifstream inFile(filePath);
    if (!inFile) {
        std::cerr << "Unable to open file: " << filePath << std::endl;
        return;
    }

    std::string line;
    int num_process;
    int num_VMA;
    bool is_line = false;
    int start, end, write, file, vp;
    char opr;

    std::getline(inFile, line);
    while (line.empty() || line[0] == '#') {
        // Skip comments and empty lines to find the first valid line
        std::getline(inFile, line);
    }
    std::istringstream(line) >> num_process; // First valid line is the number of processes

    // std::cout << "Number of processes: " << num_process << std::endl;

    for (int p = 0; p < num_process; p++) {
        // Now, for each process, read the number of VMAs
        std::getline(inFile, line);
        while (line.empty() || line[0] == '#') {
            std::getline(inFile, line);
        }

        std::istringstream iss_vmas(line);
        iss_vmas >> num_VMA;

        // Create a Process object here with its ID and number of VMAs
        Process* curr_process = new Process(p, num_VMA);
        curr_process->initialize_PTE();

        for (int v = 0; v < num_VMA; v++) {
            // Read each VMA specification
            std::getline(inFile, line);
            while (line.empty() || line[0] == '#') {
                std::getline(inFile, line);
            }

            std::istringstream iss_vma(line);
            iss_vma >> start >> end >> write >> file;

            // Add VMA to the current process
            curr_process->add_VMA(VMA_dict(v, start, end, write, file));
        }

        process_list.push_back(curr_process);
    }

    // Read Instructions 
    std::getline(inFile, line);
    while (!inFile.eof()) { // Check if we reached the end of the file
        if(line.empty() || line[0] == '#'){
            // Skip comments and empty lines
            std::getline(inFile, line);
            continue;
        }
        
        std::istringstream iss(line);
        iss >> opr >> vp;
        Instructions curr_Inst = Instructions(num_INSTR, opr, vp);
        Instr_table.push(curr_Inst);
        
        num_INSTR++;
        std::getline(inFile, line); // Get the next line for the next iteration
    }

    inFile.close();
}

bool get_next_instruction(long &inst_num, char &operation, int &vpage){
    if(Instr_table.empty()){
        return false;
    }

    Instructions curr_Inst = Instr_table.front();
    Instr_table.pop();

    operation = curr_Inst.opr;
    vpage = curr_Inst.v_page;
    inst_num = curr_Inst.inst_ID + 1;

    if(option_O_flag == true){
        curr_Inst.show_Inst();
    }

    return true;
}


/*
*********************************************************************
                    Step 3: Frame Table
*********************************************************************
*/


struct Frame_Entry{
    int frame_ID;
    Process* process_ptr = NULL;
    int virtual_page = -1;
    bool is_free = true;
    long unsigned int counter = 0; // For aging 
    long unsigned int last_used = 0; // For working set 
    
    Frame_Entry() : frame_ID(-1) {} // Default constructor
    Frame_Entry(int id): frame_ID(id){}
};


Frame_Entry Frame_Table[Frame_max_size];        // Global Frame Table
deque<Frame_Entry*> free_frame_pool;            // List of frames free to use 

void InitializeFrameTable(){
    for (int i = 0; i < num_frames; i++) {
        Frame_Table[i] = Frame_Entry(i); 
    }
}

void InitializeFramePool(){
    for (int i = 0; i < num_frames; i++) {
        free_frame_pool.push_back(&Frame_Table[i]);
    }
}

Frame_Entry *allocate_frame_from_free_list(){
    if(free_frame_pool.empty()){
        return NULL;
    }
    Frame_Entry* free_frame = free_frame_pool.front();
    free_frame_pool.pop_front();
    return free_frame;
}

/*
*********************************************************************
                    Step 4: Pager
*********************************************************************
*/

bool is_legal(Process* curr_process, int v_page, PT_entry* curr_virtual_page){
    bool vma_legal = false;
    for(int i=0; i < curr_process->num_VMA; i++){

        if(v_page >= curr_process->vma_list[i].VP_start && v_page <= curr_process->vma_list[i].VP_end){
            curr_virtual_page->write_protect = curr_process->vma_list[i].write_protected;
            curr_virtual_page->file_mapped = curr_process->vma_list[i].filemapped;
            vma_legal = true;
            break;
        }

    }
    return vma_legal;
}

void reset_PTE(PT_entry* curr_virtual_page){
    curr_virtual_page->valid = 0;
    curr_virtual_page->modified = 0;
    curr_virtual_page->referenced = 0;
    curr_virtual_page->paged_out = 0;
    curr_virtual_page->physcial_Addr = 0;
}

class Pager {
public:
    virtual ~Pager() {}
    virtual Frame_Entry* select_victim_frame() = 0;  // virtual base class   
};

Pager* my_pager = NULL;                             // Pointer to the base class


void handle_victim_frame(Frame_Entry* victim_frame){

    Process* prev_process = victim_frame->process_ptr;
    PT_entry *prev_virtual_page = &prev_process->virtual_page_table[victim_frame->virtual_page];

    prev_virtual_page->valid = 0;
    prev_virtual_page->physcial_Addr = 0;
    prev_process->process_stats["unmaps"] += 1;
    final_cost += cost_of_unmaps;

    string print_text;
    bool print_flag = false;
    if(prev_virtual_page->modified){
        if(prev_virtual_page->file_mapped){
            prev_process->process_stats["fouts"] += 1;
            final_cost += cost_of_fouts;
            print_text = " FOUT";
            print_flag = true;
        }
        else{
            prev_virtual_page->paged_out = 1;

            prev_process->process_stats["outs"] += 1;
            final_cost += cost_of_outs;
            print_text = " OUT";
            print_flag = true;
        }

        prev_virtual_page->modified = 0;
        
    }
    
    if (option_O_flag) {
        std::cout << " UNMAP " << prev_process->Process_ID << ":" << victim_frame->virtual_page << std::endl;
        if(print_flag){
            std::cout << print_text << std::endl;
        }
    }

    victim_frame->process_ptr = NULL;
    victim_frame->virtual_page = -1;
    victim_frame->counter = 0;
    // victim_frame->last_used = 0;
}


Frame_Entry *get_frame() {
    Frame_Entry* free_frame = allocate_frame_from_free_list();

    if(free_frame == NULL){
        free_frame = my_pager->select_victim_frame();
        handle_victim_frame(free_frame);
    }
    return free_frame;
}




/*
*********************************************************************
                    Step 5: Paging Algorithms
*********************************************************************
*/

class FIFO_Algo : public Pager {
private:
    int hand_ptr = 0;
    Frame_Entry* victim_frame = NULL;
public:
    Frame_Entry* select_victim_frame() override {
        if(hand_ptr < num_frames){
            victim_frame = &Frame_Table[hand_ptr]; 
            hand_ptr++;
        }
        else{
            victim_frame = &Frame_Table[0]; 
            hand_ptr = 1;
        }
        return victim_frame;
    }

};

class Random_Algo : public Pager {
private:
    int hand_rand;
    Frame_Entry* victim_frame = NULL;
public:
    Frame_Entry* select_victim_frame() override {
        hand_rand = get_random_number();
        victim_frame = &Frame_Table[hand_rand]; 
        return victim_frame;
    }
};

class Clock_Algo : public Pager {
private:
    int hand_ptr = 0;
    Frame_Entry* victim_frame = NULL;
    PT_entry* victim_virtual_page = NULL;
public:
    Frame_Entry* select_victim_frame() override {
        while(true){
            victim_frame = &Frame_Table[hand_ptr];
            victim_virtual_page = &((victim_frame->process_ptr)->virtual_page_table[victim_frame->virtual_page]);

            hand_ptr = (hand_ptr+1) % num_frames;
            if(victim_virtual_page->referenced){
                victim_virtual_page->referenced = 0;
            }
            else{
                return victim_frame;
            }
        }
    }
};


class NRU_Algo : public Pager {
private:
    int victim_ind = 0;
    Frame_Entry* victim_frame = NULL;

    int last_reset = 0;
    int frame_ind = 0;
    bool reset = false;

    Process* curr_process;
    int vpage;
    PT_entry* pt;      
    

    void resetReferenceBits() {
        reset = false;
        for (int i = 0; i < num_frames; i++) {
            Frame_Entry* ft = &Frame_Table[i];
            ft->process_ptr->virtual_page_table[ft->virtual_page].referenced = 0;
        }
    }

public:
    Frame_Entry* select_victim_frame() override {
        if (inst_num - last_reset >= 48) {
            reset = true;
            last_reset = inst_num;
        }

        int scan = 0;
        int lowest_class = 4;
        while (scan < num_frames) {
            if (frame_ind >= num_frames) frame_ind = 0;

            curr_process = Frame_Table[frame_ind].process_ptr;
            vpage = Frame_Table[frame_ind].virtual_page;
            pt = &curr_process->virtual_page_table[vpage];

            int current_class = 2 * pt->referenced + pt->modified;
            if (current_class < lowest_class) {
                lowest_class = current_class;
                victim_ind = frame_ind;
            }
            if (current_class == 0) break; // Found the optimal victim

            scan++;
            frame_ind++;
        }

        if(reset){
            resetReferenceBits();
        }

        victim_frame = &Frame_Table[victim_ind];
        frame_ind = (victim_ind + 1) % num_frames;
        return victim_frame;
    }
};

class Aging_Algo : public Pager {
private:
    int hand_ptr = 0;
    Frame_Entry* victim_frame = NULL;
    Frame_Entry* curr_frame;
public:
    Frame_Entry* select_victim_frame() override {
        uint32_t min_age = UINT32_MAX;  // Initialize to maximum possible age
        int min_index = hand_ptr;       // Start searching from the hand pointer

        for (int i = 0; i < num_frames; i++) {
            int idx = (hand_ptr + i) % num_frames;  // Circular array indexing
            curr_frame = &Frame_Table[idx];
            
            // Right shift age, simulate the passage of time
            curr_frame->counter >>= 1;
            
            // If the page was referenced since the last check, set the MSB
            if (curr_frame->process_ptr->virtual_page_table[curr_frame->virtual_page].referenced) {
                curr_frame->counter |= 0x80000000;
                curr_frame->process_ptr->virtual_page_table[curr_frame->virtual_page].referenced = 0;  // Reset reference bit
            }

            // Find the frame with the minimum age
            if (curr_frame->counter < min_age) {
                min_age = curr_frame->counter;
                min_index = idx;
                victim_frame = curr_frame;
            }
        }

        // Update the hand pointer to the next frame after the victim
        hand_ptr = (min_index + 1) % num_frames;
        return victim_frame;
    }

};

class WorkingSet_Algo : public Pager {
private:
    int hand_ptr = 0;
    Frame_Entry* victim_frame = NULL;
    Frame_Entry* current_frame;
    PT_entry* curr_virtual_page;
    int TAU = 49;

public:
    Frame_Entry* select_victim_frame() override {
        int current_hand = hand_ptr;
        
        int min_index = -1;
        unsigned long min_time = ULONG_MAX;
        unsigned long current_time = inst_num; // Assuming 'inst_num' tracks the count of instructions

        do {
            current_frame = &Frame_Table[current_hand];
            curr_virtual_page = &current_frame->process_ptr->virtual_page_table[current_frame->virtual_page];

            if (curr_virtual_page->referenced) {
                curr_virtual_page->referenced = 0;
                current_frame->last_used = current_time;
            } else {
                unsigned long time_diff = current_time - current_frame->last_used;
                if (time_diff > TAU) {
                    min_index = current_hand;
                    break;
                }
            }

            if (current_frame->last_used < min_time) {
                min_time = current_frame->last_used;
                min_index = current_hand;
            }

            current_hand = (current_hand + 1) % num_frames; // Assuming 'num_frames' is the total number of frames
        } while (current_hand != hand_ptr);

        hand_ptr = (min_index + 1) % num_frames;
        victim_frame = &Frame_Table[min_index];
        return victim_frame;
    }
};


/*
*********************************************************************
                    Step 6: Simulation
*********************************************************************
*/

void simulation(){    
    // Frame Table 
    InitializeFrameTable();
    InitializeFramePool();

    char opr = ' ';
    int v_page = -1;   
    Process* curr_process;
    bool next_instr;

    while(get_next_instruction(inst_num, opr, v_page)){
        next_instr = false;

        if(opr == 'c'){ // Context Switch
            curr_process = process_list[v_page];
            num_contxt_switch++;
            final_cost += cost_of_context_switches;
            // continue;
        }
        else if(opr == 'e'){ // Exit
            bool print_flag = false;
            std::cout << "EXIT current process " << curr_process->Process_ID << std::endl;
            
            for(int p = 0; p < PTE_max_size; p++){
                PT_entry *curr_virtual_page = &curr_process->virtual_page_table[p];

                if(curr_virtual_page->valid){

                    Frame_Entry* prev_Frame =  &Frame_Table[curr_virtual_page->physcial_Addr];
                    curr_process->process_stats["unmaps"] += 1;
                    final_cost += cost_of_unmaps;
                    
                    if(curr_virtual_page->file_mapped && curr_virtual_page->modified){
                        curr_process->process_stats["fouts"] += 1;
                        final_cost += cost_of_fouts;

                        print_flag = true;
                    }
                    // Clear Frame
                    prev_Frame->process_ptr = NULL;
                    prev_Frame->virtual_page = -1;
                    prev_Frame->is_free = true;
                    prev_Frame->counter = 0;
                    // prev_Frame->last_used = 0;
                    free_frame_pool.push_back(prev_Frame);

                    if(option_O_flag){
                        std::cout << " UNMAP " << curr_process->Process_ID << ":" << p << std::endl;
                        if(print_flag){
                            std::cout << " FOUT" << std::endl;
                        }
                    } 
                    print_flag = false;
                    
                }
                // Clear VP
                reset_PTE(curr_virtual_page);
            }
            num_proc_exit++;
            final_cost += cost_of_process_exit;
        }
        else{ // Read or Write

            final_cost += cost_of_read_write;
            PT_entry *curr_virtual_page = &curr_process->virtual_page_table[v_page];


            if (!curr_virtual_page->valid) {

                bool VMA_legal = is_legal(curr_process, v_page, curr_virtual_page);
                
                if (VMA_legal) { // Check if part of VMA  
                    // Initialise Page
                    Frame_Entry* new_Frame = get_frame(); 
                    new_Frame->process_ptr = curr_process;
                    new_Frame->virtual_page = v_page;
                    new_Frame->counter = 0;
                    new_Frame->last_used = inst_num;

                    curr_virtual_page->physcial_Addr = new_Frame->frame_ID; 
                    curr_virtual_page->valid = 1;               // Valid as it has Frame

                    // Populate the page based on its previous state
                    if (curr_virtual_page->paged_out) {
                        curr_process->process_stats["ins"] += 1;
                        final_cost += cost_of_ins;

                        if (option_O_flag) {
                            std::cout << " IN" << std::endl;
                        }
                    } else if (curr_virtual_page->file_mapped) {
                        curr_process->process_stats["fins"] += 1;
                        final_cost += cost_of_fins;

                        if (option_O_flag) {
                            std::cout << " FIN" << std::endl;
                        }
                    } else {
                        curr_process->process_stats["zeros"] += 1;
                        final_cost += cost_of_zeros;

                        if (option_O_flag) {
                            std::cout << " ZERO" << std::endl;
                        }
                    }

                    // Update Bits
                    curr_virtual_page->modified = 0;

                    // my_pager->reset_counter(newframe, INST_COUNT);

                    curr_process->process_stats["maps"] += 1;
                    final_cost += cost_of_maps;

                    if (option_O_flag) {
                        std::cout << " MAP " << new_Frame->frame_ID << std::endl;
                    }
                    

                } else { 
                // Page fault exception
                    curr_process->process_stats["segvs"] += 1;
                    final_cost += cost_of_segv;

                    if (option_O_flag) {
                        std::cout << " SEGV" << std::endl;
                    }

                    next_instr = true;
                }
            }

            if(!next_instr){
                if(opr == 'w'){
                    if(curr_virtual_page->write_protect){
                        curr_virtual_page->referenced = 1;
                        curr_process->process_stats["segprots"] += 1;
                        final_cost += cost_of_segprot;
                        
                        if (option_O_flag) {
                            std::cout << " SEGPROT" << std::endl;
                        }
                    }
                    else{
                        curr_virtual_page->referenced = 1;
                        curr_virtual_page->modified = 1;
                    }

                }
                else if(opr == 'r'){
                    curr_virtual_page->referenced = 1;
                }
            }

        }
    }
}


/*
*********************************************************************
                    Step 7: Print Summary
*********************************************************************
*/


void print_page_table(){
    for(Process* curr_process: process_list){
        std::cout << "PT[" << curr_process->Process_ID << "]:";

        for(int p=0; p < PTE_max_size; p++){
            PT_entry *curr_virtual_page = &curr_process->virtual_page_table[p];

            if(curr_virtual_page->valid){
                std::cout << " " << p << ":";

                if(curr_virtual_page->referenced){
                    std::cout << "R";
                }
                else{
                    std::cout << "-";
                }

                if(curr_virtual_page->modified){
                    std::cout << "M";
                }
                else{
                    std::cout << "-";
                }

                if(curr_virtual_page->paged_out){
                    std::cout << "S";
                }
                else{
                    std::cout << "-";
                }
            }
            else{
                if(curr_virtual_page->paged_out){
                    std::cout << " #";
                }
                else{
                    std::cout << " *";
                }
            }
        }
        std::cout << std::endl;
    }

}


void print_frame_table(){
    std::cout << "FT:";
    for(int f=0; f < num_frames; f++){
        Frame_Entry* curr_frame = &Frame_Table[f];
        if(curr_frame->virtual_page != -1){
            std::cout << " " << curr_frame->process_ptr->Process_ID << ":" << curr_frame->virtual_page;
        }
        else{
            std::cout << " *";
        }
    }
    std::cout << std::endl;
}

void print_summary(){
    for(Process* curr_process: process_list){
        std::cout << "PROC[" << curr_process->Process_ID 
                  << "]: U=" << curr_process->process_stats["unmaps"]
                  << " M=" << curr_process->process_stats["maps"]
                  << " I=" << curr_process->process_stats["ins"]
                  << " O=" << curr_process->process_stats["outs"]
                  << " FI=" << curr_process->process_stats["fins"]
                  << " FO=" << curr_process->process_stats["fouts"]
                  << " Z=" << curr_process->process_stats["zeros"]
                  << " SV=" << curr_process->process_stats["segvs"]
                  << " SP=" << curr_process->process_stats["segprots"]
                  << std::endl;
    }

    std::cout << "TOTALCOST " << num_INSTR << " " << num_contxt_switch << " " <<  num_proc_exit << " " << final_cost << " " << sizeof(PT_entry) << std::endl;
}

void print_output(){

    if(option_P_flag){
        print_page_table();
    }
    if(option_F_flag){
        print_frame_table();
    }
    if(option_S_flag){
        print_summary();
    }
}



/*
*********************************************************************
                    Step 8: Main Function
*********************************************************************
*/


int main(int argc, char* argv[]) {

    std::string algo; // For storing the algorithm type
    std::string options; // For storing the options

    int option;
    while ((option = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (option) {
            case 'f':
                num_frames = std::atoi(optarg);
                break;
            case 'a':
                switch (optarg[0]) {
                    case 'f' : {
                        // FIFO
                        my_pager = new FIFO_Algo();
                        break;
                    }
                    case 'r' : {
                        // Random
                        my_pager = new Random_Algo();
                        break;
                    }
                    case 'c' : {
                        // Clock
                        my_pager = new Clock_Algo();
                        break;
                    }
                    case 'e' : {
                        // Enhanced Second Chance / NRU
                        my_pager = new NRU_Algo();
                        break;
                    }
                    case 'a' : {
                        // Aging
                        my_pager = new Aging_Algo();
                        break;
                    }
                    case 'w' : {
                        // Working Set
                        my_pager = new WorkingSet_Algo();
                        break;
                    }
                }

                break;
            
            case 'o':
                options = optarg;
                for (char ch : options) { // Iterate through each character in options string
                    switch (ch) {
                        case 'O': { 
                            option_O_flag = true;
                            break;
                        }
                        case 'P': { 
                            option_P_flag = true;
                            break;
                        }
                        case 'S': { 
                            option_S_flag = true;
                            break;
                        }
                        case 'F': {
                            option_F_flag = true;
                            break;
                        }
                        case 'x': {
                            // OPTIONAL
                            break;
                        }
                        case 'y': {
                            // OPTIONAL
                            break;
                        }
                        case 'f': {
                            // OPTIONAL
                            break;
                        }
                        case 'a': {
                            // OPTIONAL
                            break;
                        }
                        // Add more cases as necessary
                        default: {
                            // Optionally handle unknown flags
                            break;
                        }
                    }
                }
                break;
      
            default: // '?'
                std::cerr << "Usage: " << argv[0] << " –f<num_frames> -a<algo> [-o<options>] inputfile randomfile\n";
                exit(EXIT_FAILURE);
        }
    }

    
    if (optind < argc) {
        inputfile = argv[optind++];
        randfile = argv[optind];
    }

    read_input(inputfile);
    random_stream(randfile);

    // Simulation
    simulation();
    print_output();

    return 0;
}
