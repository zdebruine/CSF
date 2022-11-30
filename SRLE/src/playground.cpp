//[[Rcpp::depends(RcppEigen)]]
//[[Rcpp::depends(RcppClock)]]
#include <RcppEigen.h>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <bits/stdc++.h>
#include <map>
#include <vector>
// #include "const_array_iterator.cpp"
// #include "GenericCSCIterator.cpp"
// #include "matrixCreator.cpp"
// #include "DeBruine's_Compressed_Matrix_Name.cpp"
// #include <Eigen/Sparse>
#include <RcppClock.h>
// #include <Rcpp.h>

using namespace std;
//using namespace //Rcpp;
// void calcTime(chrono::steady_//clock::time_point begin, chrono::steady_//clock::time_point end);
template<typename T> class const_array_iterator;
template<typename T> class GenericCSCIterator;
template<typename T> class matrixCreator;
class rng;

// template<typename T>
// Eigen::SparseMatrix<T> generateMatrix(int numRows, int numCols, double sparsity);
void iteratorBenchmark(int numRows, int numCols, double sparsity);


int main() {
    int numRows = 100;
    int numCols = 100;
    double sparsity = 20;
    iteratorBenchmark(numRows, numCols, sparsity);

    // const_array_iterator<int>* iter = new const_array_iterator<int>("input.bin");
    // int value = 0;
    // ////clock.tick("SRLE");
    // while(iter->operator bool()) {
    //     iter->operator++();
    //     if(iter->operator *() != value){
    //         cout << iter->operator *() << endl;
    //         value =  iter->operator *();
    //     }
    // }


    return 0;
}


// void calcTime(chrono::steady_//clock::time_point begin, chrono::steady_//clock::time_point end){
//     std::Rcout << "Time difference = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[µs]" << std::endl;
//     std::Rcout << "Time difference = " << std::chrono::duration_cast<std::chrono::nanoseconds> (end - begin).count() << "[ns]" << std::endl;
//     std::Rcout << "Time difference = " << std::chrono::duration_cast<std::chrono::milliseconds> (end - begin).count() << "[ms]" << std::endl;

// }

class DeBruinesComp
{
private:
    // ! Magic is currently unused
    uint8_t magic = 1;
    uint8_t delim = 0;

    size_t num_rows;
    size_t num_cols;
    size_t num_vals;

    uint8_t row_t;
    uint8_t col_t;
    uint8_t val_t;

    uint8_t *ptr;
    uint8_t *data;

    void allocate()
    {
        // ! Malloc currently allocates much more than needed
        data = (uint8_t *)malloc(num_vals * 4 * val_t);
        if (!data)
        {
            cerr << "Malloc Failed" << endl;
        }
        ptr = data;
    }

    uint8_t byte_width(size_t size)
    {
        switch (size)
        {
        case 0 ... 255:
            return 1;
        case 256 ... 65535:
            return 2;
        case 65536 ... 16777215:
            return 3;
        case 16777216 ... 4294967295:
            return 4;
        default:
            return 8;
        }
    }

public:
    // Constructor to take in an eigen sparse matrix
    // ! Is temporary, still gross, just a proof of concept
    template <typename T>
    DeBruinesComp(Eigen::SparseMatrix<T> &mat)
    {
        // find the amount of non zero elements, rows, and columns
        num_vals = mat.nonZeros();
        num_rows = mat.rows();
        num_cols = mat.cols();

        T *non_zero = new T[num_vals];
        T *indices = new T[num_rows];
        T *col_indices = new T[num_vals];

        // get a list of all the non-zero elements
        for (int k = 0; k < num_vals; ++k)
        {
            for (int i = 0; i < num_rows; ++i)
            {
                for (int j = 0; j < num_cols; ++j)
                {
                    non_zero[k] = mat.coeff(i, j);
                }
            }
        }

        // get a list of the indices of the non-zero elements
        for (int k = 0; k < num_rows; ++k)
        {
            indices[k] = mat.outerIndexPtr()[k];
        }

        // get a list of the column indices of the non-zero elements
        for (int k = 0; k < num_vals; ++k)
        {
            col_indices[k] = mat.innerIndexPtr()[k];
        }

        // call the constructor with the lists
        DeBruinesComp(non_zero, indices, col_indices, num_vals, num_rows, num_cols);
    }

    /*  Takes in a COO Matrix and converts it to a DeBruinesComp Matrix
        @param *vals: Pointer to the values of the COO Matrix
        @param *rows: Pointer to the rows of the COO Matrix
        @param *cols: Pointer to the cols of the COO Matrix
        @param val_num: Number of vals in the COO Matrix
        @param row_num: Number of rows in the COO Matrix
        @param col_num: Number of cols in the COO Matrix */
    template <typename values, typename rowcols>
    DeBruinesComp(const values *vals, const rowcols *rows, const rowcols *cols, size_t val_num, size_t row_num, size_t col_num)
    {

        // Initialize the number of rows, cols, and vals
        num_rows = row_num;
        num_cols = col_num;
        num_vals = val_num;

        size_t max_val = 0;

        // Finds max value in vals to be compressed to val_type
        // ? Could refactor to be in first loop and construct metadata between making dictionary and building runs
        for (size_t i = 0; i < num_vals; i++)
        {
            if (vals[i] > max_val)
            {
                max_val = vals[i];
            }
        }

        // Finds the smallest type that can hold the max value
        row_t = byte_width(num_rows);
        col_t = byte_width(num_cols);
        val_t = byte_width(max_val);

        allocate();

        // Construct Metadata
        memcpy(ptr, &row_t, 1);
        ptr++;

        memcpy(ptr, &col_t, 1);
        ptr++;

        memcpy(ptr, &val_t, 1);
        ptr++;

        memcpy(ptr, &num_rows, row_t);
        memcpy(ptr + row_t, &num_cols, col_t);
        ptr += row_t + col_t;

        // Leave pointer to update col pointers later
        uint64_t *col_ptr = (uint64_t *)ptr;
        col_ptr[0] = 0;
        col_ptr++;

        ptr += num_cols * 8;

        uint8_t previous_idx = 0;

        // Column Loop
        for (size_t i = 0; i < num_cols; i++)
        {
            // Create dictionary with all metadata
            map<values, vector<rowcols>> unique_vals;

            // move through data (FOR THE COLUMN) and if value is unique, add it to dictionary, if value is not unique, add index to that value in dictionary
            // First val in vector is the previous index (used for positive delta encoding)
            // TODO Refactor to only run through the column being encoded
            for (size_t j = 0; j < num_vals; j++)
            {
                if (unique_vals.count(vals[j]) == 1 && cols[j] == i)
                {
                    // Val Exists in Dict

                    size_t delta = rows[j] - unique_vals[vals[j]][0];

                    unique_vals[vals[j]].push_back(delta);

                    unique_vals[vals[j]][0] = rows[j];
                }
                else if (cols[j] == i)
                {
                    // Val does not Exist in dict

                    vector<rowcols> temp = {rows[j], rows[j]};
                    unique_vals[vals[j]] = temp;
                }
            }

            // Loop through dictionary and construct compression
            for (auto i : unique_vals)
            {
                memcpy(ptr, &i.first, val_t);
                ptr += val_t;

                uint8_t *idx = ptr;
                uint8_t idx_t = byte_width(*max_element(i.second.begin(), i.second.end()));
                memcpy(ptr, &idx_t, 1);
                ptr++;

                // Construct Run
                for (auto j : i.second)
                {
                    if (j != i.second[0])
                    {
                        memcpy(ptr, &j, idx_t);
                        ptr += idx_t;
                    }
                }

                for (size_t j = 0; j < idx_t; j++)
                {
                    memcpy(ptr, &delim, 1);
                    ptr++;
                }

                previous_idx = idx_t;
            }

            // update col_ptr
            if (i != num_cols - 1)
            {
                size_t col_location = ptr - data;
                memcpy(col_ptr, &col_location, 8);
                col_ptr++;
            }
        }

        // Chop off end delimiters
        ptr -= previous_idx;

        // Resize data to fit actual size
        data = (uint8_t *)realloc(data, ptr - data);
    }

    char* getData(){
        return (char*)data;
    }

    ~DeBruinesComp()
    {
        free(data);
    }

    void print()
    {
        cout << "Printing DeBruinesComp Matrix" << endl;
        cout << "Row Type: " << (int)row_t << endl;
        cout << "Col Type: " << (int)col_t << endl;
        cout << "Val Type: " << (int)val_t << endl;
        cout << "Num Rows: " << num_rows << endl;
        cout << "Num Cols: " << num_cols << endl;
        cout << "Num Vals: " << num_vals << endl;
        cout << "Data: " << endl;
        for (size_t i = 0; i < ptr - data; i++)
        {
            cout << (int)data[i] << " ";
        }
        cout << endl;
        cout << endl;
    }

    // Write to file
    void write(string filename)
    {
        ofstream file(filename, ios::out | ios::binary);
        file.write((char *)data, ptr - data);
        file.close();
    }

    // Read from file
    void read(string filename)
    {
        // Open file
        ifstream file(filename, ios::in | ios::binary);

        // Get file size
        file.seekg(0, ios::end);
        size_t size = file.tellg();
        file.seekg(0, ios::beg);

        // Allocate memory
        data = (uint8_t *)malloc(size);

        // Read data
        file.read((char *)data, size);
        file.close();

        // Set pointer and itialize variables
        ptr = data;
        row_t = *ptr;
        ptr++;
        col_t = *ptr;
        ptr++;
        val_t = *ptr;
        ptr++;
        memcpy(&num_rows, ptr, row_t);
        memcpy(&num_cols, ptr + row_t, col_t);
        ptr += row_t + col_t;

        // Put pointer at end of data
        ptr += size - (ptr - data);
    }
};


template<typename T>
class GenericCSCIterator {
    //todo:
    //clean the vocabulary
    private:
        vector<int> row = vector<int>();
        vector<int> col = vector<int>();
        vector<T> values = vector<T>();
        int* rowPtr;
        int* colPtr;
        T* valuePtr;
        int* end;



    public:

    GenericCSCIterator(Eigen::SparseMatrix<T> myMatrix) {
        for (int i=0; i < myMatrix.outerSize(); ++i)
            for(typename Eigen::SparseMatrix<T>::InnerIterator it(myMatrix, i); it; ++it){               
                row.push_back(it.row());
                col.push_back(it.col());
                values.push_back(it.value());
            }
        rowPtr = &row[0];
        colPtr = &col[0];
        valuePtr = &values[0];
        end = rowPtr + row.size();
    }


    //todo make this return type T 
    T& operator * () {return *valuePtr;}
    
    int getRow () {return *rowPtr;}

    int getCol () {return *colPtr;}

    const int operator++() { 
        //Iterate over myMatrix
        rowPtr++;
        valuePtr++;
        return *rowPtr;
    }

    // equality operator
    operator bool() {return rowPtr != end;}

};

class rng {
   private:
    uint64_t state;

   public:
    rng(uint64_t state) : state(state) {}

    void advance_state() {
        state ^= state << 19;
        state ^= state >> 7;
        state ^= state << 36;
    }

    uint64_t operator*() const {
        return state;
    }

    uint64_t rand() {
        uint64_t x = state ^ (state << 38);
        x ^= x >> 13;
        x ^= x << 23;
        
        return x;
    }

    uint64_t rand(uint64_t i) {
        // advance i
        i ^= i << 19;
        i ^= i >> 7;
        i ^= i << 36;

        // add i to state
        uint64_t x = state + i;

        // advance state
        x ^= x << 38;
        x ^= x >> 13;
        x ^= x << 23;

        return x;
    }

    uint64_t rand(uint64_t i, uint64_t j) {
        uint64_t x = rand(i);

        // advance j
        j ^= j >> 7;
        j ^= j << 23;
        j ^= j >> 8;

        // add j to state
        x += j;

        // advance state
        x ^= x >> 7;
        x ^= x << 53;
        x ^= x >> 4;

        return x;
    }

    template <typename T>
    T sample(T max_value) {
        return rand() % max_value;
    }

    template <typename T>
    T sample(uint64_t i, T max_value) {
        return rand(i) % max_value;
    }

    template <typename T>
    T sample(uint64_t i, uint64_t j, T max_value) {
        return rand(i, j) % max_value;
    }

    template <typename T>
    bool draw(T probability) {
        return sample(probability) == 0;
    }

    template <typename T>
    bool draw(uint64_t i, T probability) {
        return sample(i, probability) == 0;
    }

    template <typename T>
    bool draw(uint64_t i, uint64_t j, T probability) {
        sample(i, j, probability);
        return sample(i, j, probability) == 0;
    }

    template <typename T>
    double uniform() {
        T x = (T)rand() / UINT64_MAX;
        return x - std::floor(x);
    }

    template <typename T>
    double uniform(uint64_t i) {
        T x = (T)rand(i) / UINT64_MAX;
        return x - std::floor(x);
    }

    template <typename T>
    double uniform(uint64_t i, uint64_t j) {
        T x = (T)rand(i, j) / UINT64_MAX;
        return x - std::floor(x);
    }
};

template<typename T>
class const_array_iterator {
    //todo:
    //clean the vocabulary
    private:
        uint32_t magicByteSize; //= params[0];
        uint32_t rowType;       //= params[1];
        uint32_t nRows;         //= params[2];
        uint32_t colType;       //= params[3];
        uint32_t nCols;         //= params[4];
        uint32_t valueWidth;    //= params[5];
        uint32_t oldIndexType;  //= params[6];        
        int newIndexWidth; //basically how many bytes we read, NOT ACTUALLY THE TYPE
        char* end;
        char* fileData;
        void* arrayPointer;
        uint64_t index = 0;
        T value;
        uint64_t sum = 0;

    public:
      

   const_array_iterator(const char* filePath) {

        //set up the iterator
        readFile(filePath);
        //read first 28 bytes of fileData put it into params -> metadata
        uint32_t params[7];
        
        memcpy(&params, arrayPointer, 32); //28 is subject to change depending on magic bytes
        arrayPointer+=32; //first delimitor is 4 bytes

        magicByteSize = params[0];
        rowType       = params[1];
        nRows         = params[2];
        colType       = params[3];
        nCols         = params[4];
        valueWidth    = params[5];
        oldIndexType  = params[6];

        memcpy(&value, arrayPointer, valueWidth);
        arrayPointer += valueWidth;
        newIndexWidth =  static_cast<int>(*static_cast<uint8_t*>(arrayPointer));
        arrayPointer++; //this should make it point to first index

        // cout << "value: " << value << endl;
        // cout << "newIndexWidth: " << newIndexWidth << endl;

        // for debugging
        //  for(int i = 0; i < 7; i++) {
        //      cout << i << " " << params[i] << endl;
        // }


    }//end of constructor


    //todo make this return type T 
    T& operator * () {return value;}; 
    
    //EXPERIMENTAL INCREMENT OPERATOR -> NOT DECREMENT
    const uint64_t operator --() {
        uint64_t newIndex = 0; 

        switch (newIndexWidth){
            case 1:
                newIndex = static_cast<uint64_t>(*static_cast<uint8_t*>(arrayPointer));
                break;
            case 2:
                newIndex =  static_cast<uint64_t>(*static_cast<uint8_t*>(arrayPointer));
                break;
            case 4:
                newIndex =  static_cast<uint64_t>(*static_cast<uint8_t*>(arrayPointer));
                break;
            case 8:
                newIndex =  static_cast<uint64_t>(*static_cast<uint8_t*>(arrayPointer));
                break;
            default:
                cerr << "Invalid width" << endl;
                break;
        }
        arrayPointer += newIndexWidth;

        if(newIndex == 0 && index != 0){ //change that
            
            memcpy(&value, arrayPointer, valueWidth);
            arrayPointer += valueWidth; 
            
            memcpy(&newIndexWidth, arrayPointer, 1);
            arrayPointer++;
            
            // cout << endl << "value: " << value << endl;
            // cout << "newIndexWidth: " << newIndexWidth << endl;
            
            memset(&index, 0, 8);
            memcpy(&index, arrayPointer, newIndexWidth);

        }
        return index += newIndex;
    }

    // template<typename indexType> 
    const uint64_t operator++() { 
        //TODO template metaprogramming
        //todo through an exception if we request something smaller than the size of the index

        uint64_t newIndex = 0; //get rid of in future versions

        memcpy(&newIndex, arrayPointer, newIndexWidth);
        arrayPointer += newIndexWidth;
        sum += value;

        if(newIndex == 0 && index != 0){ //change that
            
            memcpy(&value, arrayPointer, valueWidth);
            arrayPointer += valueWidth; 
            
            memcpy(&newIndexWidth, arrayPointer, 1);
            arrayPointer++;
            
            // cout << endl << "value: " << value << endl;
            // cout << "newIndexWidth: " << newIndexWidth << endl;
            
            memset(&index, 0, 8);
            memcpy(&index, arrayPointer, newIndexWidth);

        }
        return index += newIndex;

    }


    // equality operator
    operator bool() { return end >= arrayPointer;} //change to not equal at the end


    // reads in the file and stores it in a char* 
    // inline void readFile(string filePath){ 
    //     ifstream fileStream;
    //     fileStream.open(filePath, ios::binary | ios::out);
        
    //     fileStream.seekg(0, ios::end);
    //     int sizeOfFile = fileStream.tellg();
    //     fileData = (char*)malloc(sizeof(char*)*sizeOfFile);

    //     fileStream.seekg(0, ios::beg);
    //     fileStream.read(fileData, sizeOfFile);
        
    //     fileStream.close();

    //     arrayPointer = fileData;
    //     end = fileData + sizeOfFile;
    //     }

    //marginally faster 
    inline void readFile(const char* filePath){
        //read a file using the C fopen function and store to fileData
        FILE* file = fopen(filePath, "rb");
        fseek(file, 0, SEEK_END);
        int sizeOfFile = ftell(file);
        fileData = (char*)malloc(sizeof(char*)*sizeOfFile);
        fseek(file, 0, SEEK_SET);
        fread(fileData, sizeOfFile, 1, file);
        fclose(file);
        // cout << "Size of file: " << sizeOfFile << endl;
        arrayPointer = fileData;
        end = fileData + sizeOfFile;
    }
};

template <typename T>
Eigen::SparseMatrix<T> generateMatrix(int numRows, int numCols, double sparsity){
    //generate a random sparse matrix
    uint64_t favoriteNumber = 11515616;
    rng randMatrixGen = rng(favoriteNumber);

    Eigen::SparseMatrix<T> myMatrix(numRows, numCols);
    myMatrix.reserve(Eigen::VectorXi::Constant(numRows, numCols));

    for(int i = 0; i < numRows; i++){
        for(int j = 0; j < numCols; j++){
            if(randMatrixGen.draw<int>(i,j, sparsity)){
                myMatrix.insert(i, j) = 100 * randMatrixGen.uniform<double>(j);
            }
        }
    }
    return myMatrix;
}


//[[Rcpp::export]]
void iteratorBenchmark(int numRows, int numCols, double sparsity) {
    Rcpp::Clock clock;
    //TO ENSURE EVERYTHING WORKS, THE TOTAL SUM OF ALL VALUES IS CALUCLATED AND SHOULD PRINT THE SAME NUMBER FOR EACH ITERATOR
    uint64_t total = 0;
    int value = 0;
    string fileName = "input.bin";


    Eigen::SparseMatrix<int> myMatrix(numRows, numCols);
    myMatrix.reserve(Eigen::VectorXi::Constant(numRows, numCols));
    myMatrix = generateMatrix<int>(numRows, numCols, sparsity);
    myMatrix.makeCompressed(); 

    // DeBruinesComp myCompression(myMatrix);
    // myCompression.print();
    // myCompression.write("test.bin");

    // cout << "Testing SRLE" << endl;
    // const_array_iterator<int>* iter = new const_array_iterator<int>(fileName.c_str());
    // clock.tick("SRLE w/ memcpy");
    // while(iter->operator bool()) {
    //     iter->operator++();
    //     total += iter->operator*();
    //     if(iter->operator *() != value){
    //         value =  iter->operator *();
    //     }
    // }
    // clock.tock("SRLE w/ memcpy");
    // cout << "SRLE (N) Total: " << total << endl;
    // cout << "SRLE Total: " << total << endl;

    //////////////////////////////Experimental Iterator//////////////////////////////
    total = 0;
    cout << "Testing Experimental Iterator" << endl;
    const_array_iterator<int>* newIter = new const_array_iterator<int>(fileName.c_str());
    clock.tick("SRLE w/ void*");
    while(newIter->operator bool()) {
        newIter->operator--();
        total += newIter->operator*();
        if(newIter->operator *() != value){
            value =  newIter->operator *();
        }
    }
    // cout << "SRLE (E) Total: " << total << endl;
    clock.tock("SRLE w/ void*");

    //////////////////////////////CSC innerIterator////////////////////////////////
    //generating a large random eigen sparse
    cout << "Testing Eigen" << endl;
    total = 0;



    //begin timing
    clock.tick("Eigen");
    Eigen::SparseMatrix<int>::InnerIterator it(myMatrix, 0);
    for (int i=0; i<numRows; ++i){
        for (Eigen::SparseMatrix<int>::InnerIterator it(myMatrix, i); it; ++it){
            total += it.value();
        }
    }
    clock.tock("Eigen");
    //cout << "InnerIterator Total: " << total << endl;


    //////////////////////////////GENERIC CSC Iterator////////////////////////////////
    cout << "Testing CSC Iterator" << endl;
    total = 0;
    clock.tick("CSC");
    GenericCSCIterator<int> iter2 = GenericCSCIterator<int>(myMatrix);
    while(iter2.operator bool()){
        total += iter2.operator *();
        iter2.operator++();
    }
    clock.tock("CSC");
    //cout << "CSC Total: " << total << endl;

    clock.stop("Iterators");
}