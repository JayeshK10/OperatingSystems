#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace std;

/*
***********************************************************
                    Step 1: Tokenizer 
***********************************************************
*/

bool getToken(std::istream& in_stream, std::string& token, int& line_num, int& char_pos) {
    static std::string curr_line;                 // Store line
    static int int_line_num = 0;                  // Number of current line
    static const char* delimiter = " \t\n";       // Space between token 
    static char* next_token = nullptr;
    static int prev_token_end = 0;                // End position of previous character for end of file offset 
    static bool endOfFileReported = false;
    static std::string buffer;                    // Dynamic buffer to read each line

    if (endOfFileReported) { // Reset state if needed
        int_line_num = 0;
        prev_token_end = 0;
        endOfFileReported = false;
        next_token = nullptr;
    }

    while (next_token == nullptr) {
        if (!std::getline(in_stream, curr_line)) {
            if (!endOfFileReported) {
                line_num = int_line_num;
                char_pos = prev_token_end == 0 ? static_cast<int>(curr_line.length()) + 1 : prev_token_end;
                endOfFileReported = true;
            }
            return false; // End of file
        }
        int_line_num++;
        buffer = curr_line; // Use dynamic string to avoid strcpy and potential buffer overflow
        next_token = std::strtok(&buffer[0], delimiter);
        prev_token_end = 0; // Reset buffer position for new line
    }

    token = std::string(next_token); // Store token (one at a time)
    int token_pos = static_cast<int>(std::strstr(curr_line.c_str() + prev_token_end, token.c_str()) - curr_line.c_str()) + 1;

    // Update output values for a token
    line_num = int_line_num;
    char_pos = token_pos;

    // Update prev_token_end to the position after the current token, considering the length of the token
    prev_token_end = token_pos + static_cast<int>(token.length());
    next_token = std::strtok(nullptr, delimiter);

    if (next_token == nullptr && !endOfFileReported) {
        prev_token_end = static_cast<int>(curr_line.length()) + 1; // Adjust for the length of the line including trailing spaces
    }

    return true;
}



/*
***********************************************************
                    Step 2: Read inputs and Parse Errors
***********************************************************
*/
void __parseerror(int errcode, int line_no, int offset) {
    static const char* errstr[] = {
        "NUM_EXPECTED",             //0: Number expect, anything >= 2^30 is not a number either
        "SYM_EXPECTED",             //1: Symbol Expected
        "MARIE_EXPECTED",            //2: Addressing Expected which is M/A/R/I/E
        "SYM_TOO_LONG",             //3: Symbol Name is too long
        "TOO_MANY_DEF_IN_MODULE",   //4: > 16
        "TOO_MANY_USE_IN_MODULE",   //5: > 16
        "TOO_MANY_INSTR"            //6: total num_instr exceeds memory size (512)
    };

    std::cout << "Parse Error line " << line_no << " offset " << offset << ": " << errstr[errcode] << std::endl;
    exit(1);
}

// readInt function
bool isInteger(const string& token, int& line_number, int& token_pos) {
    for (char c : token) {
        if (!isdigit(c)) {
            // Parse Error: NUM_EXPECTED
            __parseerror(0, line_number, token_pos);
            return false;
        }
    }
    return true;
}

int readInt(ifstream& test_file, int& line_number, int& token_pos) {
    string token;
    if (getToken(test_file, token, line_number, token_pos)) {
        if(isInteger(token, line_number, token_pos)){
            return stoi(token);
        }
        else{
            // Invalid
            return -1;
        }
    }
    // No Token
    return -1; 
}

// readSymbol function
bool isSymbol(const string& token, int& line_number, int& token_pos) {
    if (token.length() > 16){
        // Parse Error: SYM_TOO_LONG
        __parseerror(3, line_number, token_pos);
        return false;
    }
    else if (token.empty() || !isalpha(token[0])) {
        // Parse Error: SYM_EXPECTED
        __parseerror(1, line_number, token_pos);
        return false;
    }
    else{
        for (char c : token) {
            if (!isalnum(c)) {
                // Parse Error: SYM_EXPECTED
                __parseerror(1, line_number, token_pos);
                return false;
            }
        }
    }
    return true;
}

string readSymbol(ifstream& test_file, int& line_number, int& token_pos) {
    string token;
    if (getToken(test_file, token, line_number, token_pos)) {
        if(isSymbol(token, line_number, token_pos)){
            return token;
        }
    }
    // No Token
    // Parse Error: SYM_EXPECTED
    __parseerror(1, line_number, token_pos);
    return ""; 
}

// readMARIE function
bool isMARIE(const string& token, int& line_number, int& token_pos) {
    if (token.length() == 1 && string("MARIE").find(token) != string::npos){
        return true;
    }
    // Parse Error: MARIE_EXPECTED
    __parseerror(2, line_number, token_pos);
    return false;
}

char readMARIE(ifstream& test_file, int& line_number, int& token_pos) {
    string token;
    if (getToken(test_file, token, line_number, token_pos)) {
        if(isMARIE(token, line_number, token_pos)){
            return token[0];
        }
        
    }
    // No MARIE Token 
    // Parse Error: MARIE_EXPECTED
    __parseerror(2, line_number, token_pos);
    return '\0';
}


/*
***********************************************************
                    Step 3: Symbol Table
***********************************************************
*/
struct Symbol {
    string name;
    int value; // Absolute address
};

// Each element is a tuple: <symbol name, symbol value, redefined flag, defined in module number>
std::vector<std::tuple<std::string, int, bool, int>> symbolTable2D; 

std::vector<int> moduleBaseTable; // Stores the base address for each module

void createSymbol(const std::string& sym, int val, int moduleNumber, int& line_number, int& token_pos) {
    bool found = false;
    for (auto& symbol : symbolTable2D) {
        if (std::get<0>(symbol) == sym) {
            // Symbol exists, mark as redefined but don't update other values
            std::cout << "Warning: Module " << moduleNumber-1 << ": " << sym << " redefinition ignored" << std::endl;
            std::get<2>(symbol) = true; // Mark this symbol as redefined
            found = true;
            break;
        }
    }
    if (!found) {
        symbolTable2D.push_back(std::make_tuple(sym, val, false, moduleNumber)); // Redefined flag is initially false
    }
}



void printSymbolTable2D() {
    std::cout << "Symbol Table" << std::endl;
    for (const auto& symbol : symbolTable2D) {
        std::cout << std::get<0>(symbol) << "=" << std::get<1>(symbol);
        if (std::get<2>(symbol)) { // If the symbol was redefined
            std::cout << " Error: This variable is multiple times defined; first value used";
        }
        std::cout << std::endl;
    }
}


/*
***********************************************************
                    Step 4: Pass1
***********************************************************
*/

void Pass1(ifstream& test_file, int& line_number, int& token_pos) {
    int currentBaseAddress = 0; // Cumulative base address for the modules
    int moduleNumber = 0;
    moduleBaseTable.push_back(currentBaseAddress);

    while (true) {
        moduleNumber++;
        int defCount = readInt(test_file, line_number, token_pos); // Read number of definitions

        if (defCount < 0){
            break;// Check for end of input
        } 
        else if(defCount > 16){
            // Parse Error: TOO_MANY_DEF_IN_MODULE
            __parseerror(4, line_number, token_pos);
        }
        
        // Process symbol definitions
        for (int i = 0; i < defCount; ++i) {
            string sym = readSymbol(test_file, line_number, token_pos); 
            int val = readInt(test_file, line_number, token_pos);
            // Adjust the relative address to the absolute address
            createSymbol(sym, val + currentBaseAddress, moduleNumber, line_number, token_pos);
        }

        // Skip usecount and associated symbols as they're not used in Pass1
        int useCount = readInt(test_file, line_number, token_pos);

        if (useCount > 16){
            // Parse Error: TOO_MANY_USE_IN_MODULE
            __parseerror(5, line_number, token_pos);
        }

        for (int i = 0; i < useCount; ++i) {
            string sym = readSymbol(test_file, line_number, token_pos); // Just read and ignore for now
        }

        // Process instructions to determine module length and update the base address
        int instCount = readInt(test_file, line_number, token_pos);

        // Check each symbol's relative address within the current module
        // After processing instructions for a module and updating currentBaseAddress
        for (auto& symbol : symbolTable2D) {
            if (std::get<3>(symbol) == moduleNumber) { // Symbol belongs to the current module
                int symbolVal = std::get<1>(symbol); // The absolute address of the symbol
                int relativeAddress = symbolVal - moduleBaseTable[moduleNumber - 1]; // Calculate relative address
                
                // instCount is the number of instructions in the current module, determining its size
                if (relativeAddress >= instCount) { // If relative address exceeds module size
                    std::cout << "Warning: Module " << moduleNumber-1 << ": " << std::get<0>(symbol)
                            << "=" << relativeAddress << " valid=[0.." << (instCount - 1) 
                            << "] assume zero relative" << std::endl;
                    std::get<1>(symbol) = moduleBaseTable[moduleNumber - 1]; // Reset symbol's value to base address of the module
                }
            }
        }
        
        currentBaseAddress += instCount; // Update cumulative base address for the next module
        moduleBaseTable.push_back(currentBaseAddress);

        if (currentBaseAddress > 512){
            // Parse Error: TOO_MANY_INSTR
            __parseerror(6, line_number, token_pos);
        }
        
        // Assuming instCount might be processed for other checks, but not shown here
        for (int i = 0; i < instCount; ++i) {
            char addrmode = readMARIE(test_file, line_number, token_pos); // Read addressing mode
            int instr = readInt(test_file, line_number, token_pos); // Read <opcode,operand>
        }
    }
    printSymbolTable2D();
}



/*
***********************************************************
                    Step 5: Memory Map
***********************************************************
*/

int memory_counter = 0;

std::vector<std::pair<int, std::string>> memoryMap; // Store instructions and errors

std::string normalError(int errcode, const std::string& additionalInfo = "") {
    std::unordered_map<int, std::string> errstr = {
        {3, "Error: {} is not defined; zero used"}, 
        {6, "Error: External operand exceeds length of uselist; treated as relative=0"},
        {8, "Error: Absolute address exceeds machine size; zero used"},
        {9, "Error: Relative address exceeds module size; relative zero used"},
        {10, "Error: Illegal immediate operand; treated as 999"},
        {11, "Error: Illegal opcode; treated as 9999"},
        {12, "Error: Illegal module operand ; treated as module=0"},
    };

    std::string message = errstr[errcode];                      // Retrieve the error message template

    if (errcode == 3){
        size_t pos = message.find("{}");                            // Find the placeholder position
        if(!additionalInfo.empty()){
            message.replace(pos, 2, additionalInfo);
        }
        else{
            message.erase(pos, 2);
        }
    }
    return message;
}

// Rule 4
void markSymbolAsUsed(const std::string& symbol, std::vector<std::tuple<std::string, int, bool, int>>& availableSymbols) {
    auto it = std::find_if(availableSymbols.begin(), availableSymbols.end(),
                           [&symbol](const auto& symTuple) { return std::get<0>(symTuple) == symbol; });

    if (it != availableSymbols.end()) {
        // Symbol found, now remove it
        availableSymbols.erase(std::remove_if(availableSymbols.begin(), availableSymbols.end(),
                                              [&symbol](const auto& symTuple) { return std::get<0>(symTuple) == symbol; }),
                               availableSymbols.end());
    }
    // If the symbol is not found, this function does nothing
}


void printWarningsForUnusedSymbols(const std::vector<std::tuple<std::string, int, bool, int>>& availableSymbols) {
    std::cout << std::endl;
    for (const auto& symbolTuple : availableSymbols) {
        const std::string& symbol = std::get<0>(symbolTuple);
        int moduleNumber = std::get<3>(symbolTuple);
        std::cout << "Warning: Module " << moduleNumber - 1 << ": " << symbol << " was defined but never used" << std::endl;
    }
}



void ProcessInstruction(int& instr, char addrMode, int moduleNumber, 
                        const std::vector<int>& moduleBaseTable, 
                        const std::vector<std::string>& currentUseList, 
                        std::vector<bool> &usedSymbols,
                        const std::vector<std::tuple<std::string, int, bool, int>>& symbolTable2D,
                        std::vector<std::pair<int, std::string>>& memoryMap, 
                        int instructionCount,
                        std::vector<std::tuple<std::string, int, bool, int>>& availableSymbols) {
    int opcode = instr / 1000;
    int operand = instr % 1000;
    std::string errorMessage;
    // std::string printMessage;

    if (opcode >= 10) {
        errorMessage = normalError(11); // "Error: Illegal opcode; treated as 9999"
        instr = 9999; // Set to error code
        // memoryMap.push_back({instr, errorMessage});

        printf("%03d", memory_counter);
        std::cout << ": " << instr << " " << errorMessage << std::endl;
        memory_counter++;

        return;
    }
 
    switch (addrMode) {
        case 'M': { // Module number address
            if (operand < moduleBaseTable.size()-1) {
                int targetModuleBase = moduleBaseTable[operand];    // Get the base address of the target module
                instr = opcode * 1000 + targetModuleBase;
            } else {
                // If he operand specifies an invalid module number
                errorMessage = normalError(12);                     
                instr = opcode * 1000;                              // Reset operand to zero, retaining opcode
            }
        } break;
        case 'A': { // Absolute address
            if (operand >= 512) {
                errorMessage = normalError(8);
                instr = opcode * 1000;                              // Reset operand to zero, retaining opcode
            }
        } break;
        case 'R': { // Relative address
            int moduleBase = moduleBaseTable[moduleNumber-1];
            if (operand >= instructionCount) {
                errorMessage = normalError(9);                      // "Error: Relative address exceeds module size; relative zero used"
                instr = opcode * 1000+ moduleBase;                  // Reset operand to zero, retaining opcode
            } else {
                instr = opcode * 1000 + operand + moduleBase;       // Correctly adjust relative address
            }
        } break;
        case 'I': { // Immediate operand
            if (operand >= 900) {
                errorMessage = normalError(10);
                instr = opcode * 1000 + 999;                        // Adjust operand to 999, retaining opcode
            }
        } break;
        case 'E': { // External address
            if (operand < currentUseList.size()) {

                // For Rule 7
                usedSymbols[operand] = true;                        

                std::string symbol = currentUseList[operand];

                // Rule 4
                markSymbolAsUsed(symbol, availableSymbols);

                auto it = std::find_if(symbolTable2D.begin(), symbolTable2D.end(),
                                        [&symbol](const auto& symTuple) { return std::get<0>(symTuple) == symbol; });
                if (it != symbolTable2D.end()) {
                    instr = opcode * 1000 + std::get<1>(*it);
                } else {
                    errorMessage = normalError(3, symbol);
                    instr = opcode * 1000;                          // Undefined symbol error code
                }
            } else {
                errorMessage = normalError(6);
                instr = opcode * 1000 + moduleBaseTable[moduleNumber-1]; // Reset operand to zero, retaining opcode
            }
        } break;
        // default:
        //     errorMessage = "";
        //     break;
    }

    // Print Memory Map
    printf("%03d: %04d", memory_counter, instr);
    if (!errorMessage.empty()) {
        std::cout << " " << errorMessage;
    }
    std::cout << std::endl;

    memory_counter++;
}


/*
***********************************************************
                    Step 6: Pass2
***********************************************************
*/

void Pass2(
    ifstream& test_file, 
    int& line_number, 
    int& token_pos, 
    const std::vector<int>& moduleBaseTable, 
    const std::vector<std::tuple<std::string, int, bool, int>>& symbolTable2D) {
    int moduleNumber = 0;
    std::vector<std::string> currentUseList; // Temporary use list for the current module

    std::vector<std::tuple<std::string, int, bool, int>> availableSymbols = symbolTable2D; // For Rule 4

    // Printing Memory Map
    std::cout << std::endl;
    std::cout << "Memory Map" << std::endl;

    while (true) {
        moduleNumber++;
        int defCount = readInt(test_file, line_number, token_pos); // Read number of definitions
        if (defCount < 0) {
            break; // End of input
        } 
        
        // Skip processing symbol definitions (Done in Pass 1)
        for (int i = 0; i < defCount; ++i) {
            string sym = readSymbol(test_file, line_number, token_pos); 
            int val = readInt(test_file, line_number, token_pos);
            // Just Ignore them
        }

        // Process use list for the current module
        int useCount = readInt(test_file, line_number, token_pos);
        
        for (int i = 0; i < useCount; ++i) {
            string sym = readSymbol(test_file, line_number, token_pos);
            currentUseList.push_back(sym); // Temporarily store the symbol for processing
        }

        std::vector<bool> usedSymbols(currentUseList.size(), false);

        // Process instructions
        int instCount = readInt(test_file, line_number, token_pos);
        for (int i = 0; i < instCount; ++i) {
            char addrMode = readMARIE(test_file, line_number, token_pos);
            int instr = readInt(test_file, line_number, token_pos); // <opcode, operand> = <1,000>
            
            // Process each instruction
            ProcessInstruction(instr, addrMode, moduleNumber, moduleBaseTable, currentUseList, usedSymbols, symbolTable2D, memoryMap, instCount, availableSymbols);
        }

        // RULE 7
        for (size_t i = 0; i < currentUseList.size(); ++i) {
            if (!usedSymbols[i]) { // If the symbol was not used
                std::cout << "Warning: Module " << moduleNumber-1 << ": uselist[" << i << "]=" << currentUseList[i] << " was not used" << std::endl;
            }
        }
        // Clear the currentUseList for the next module
        currentUseList.clear();
        usedSymbols.clear();
    }

    // RULE 4
    printWarningsForUnusedSymbols(availableSymbols);
}



/*
***********************************************************
                    Step 7: Main Funciton 
***********************************************************
*/
int main(int argc, char** argv) {
    if(argc == 1) {
		cout << "Input File required";
        return 0;
	}
    else if(argc > 2){
		cout << "Single Input File Only";
        return 0;
    }
    else{
        const char* test_file_path = argv[1];

        ifstream test_file ( test_file_path );// Input file as stream 
        int line_number = 0, char_pos = 0;
        Pass1(test_file, line_number, char_pos);
        test_file.close();
        
        // ifstream test_file ( test_file_path );// Input file as stream 
        test_file.open(test_file_path); // Reopen File
        line_number = 0;
        char_pos = 0;
        Pass2(test_file, line_number, char_pos, moduleBaseTable, symbolTable2D);
        test_file.close();

        return 0;
    }
}
