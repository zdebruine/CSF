
/*
Version 2.1
By: Skyler Ruiter
Content: This is a file to build the constructor for a new data structure and compression algorithm called CSF.
*/

#include "CSF_Lib.hpp"

using std::cout;
using std::endl;

namespace CSF {

    // Class for the CSF matrix

    class SparseMatrix {
    private:
        //* Constructor data

        // Compression delimiter
        uint8_t delim = 0;

        // dimensions and size of sparse matrix
        uint32_t num_rows;
        uint32_t num_cols;
        uint32_t num_nonzeros;

        // Data types for matrix values_t and indices
        uint32_t row_t;
        uint32_t col_t;
        uint32_t val_t;

        // compression size
        size_t compression_size;

        bool is_allocated = false;

        // void pointers for compression
        void* comp_ptr;
        void* begin_ptr;

        // Function to allocate memory for compression
        void allocate_memory() {

            // Malloc memory for the data, never worse then CSC so allocate CSC amount
            // space for the value and row, col indicies, and a buffer zone

            size_t csc_size = num_nonzeros * val_t + num_nonzeros * row_t + num_cols * col_t + 300;

            begin_ptr = malloc(csc_size);

            // Check if memory was allocated
            if (!begin_ptr) {
                throw std::bad_alloc();
            }

            is_allocated = true;

            // Set the pointer to the start of the data
            comp_ptr = begin_ptr;
        }

        // Finds the amount of bytes needed to store the given value
        uint8_t byte_width(size_t size) {
            switch (size) {
            case 0 ... 255:
                return 1;
            case 256 ... 65535:
                return 2;
            case 65536 ... 4294967295:
                return 4;
            default:
                return 8;
            }
        }

    public:
        // Eigen Wrapper Constructor
        // TODO make an optimized dedicated eigen constuctor
        template <typename T>
        SparseMatrix(Eigen::SparseMatrix<T>& mat) {

            mat.makeCompressed();

            size_t nnz = mat.nonZeros();
            T* vals_arr = new T[nnz];
            T* indexes_arr = new T[nnz];
            T* col_p_arr = new T[mat.outerSize() + 1];

            // Copy data from Eigen
            std::memcpy(vals_arr, mat.valuePtr(), nnz * sizeof(T));
            std::memcpy(indexes_arr, mat.innerIndexPtr(), nnz * sizeof(T));
            std::memcpy(col_p_arr, mat.outerIndexPtr(), (mat.outerSize() + 1) * sizeof(T));

            T** vals = &vals_arr;
            T** indexes = &indexes_arr;
            T** col_p = &col_p_arr;

            // Construct CSF
            CSF::SparseMatrix tempMatrix = CSF::SparseMatrix(vals, indexes, col_p, nnz, mat.rows(), mat.cols());


            num_rows = tempMatrix.num_rows;
            num_cols = tempMatrix.num_cols;
            num_nonzeros = tempMatrix.num_nonzeros;

            // Data types for matrix values_t and indices
            row_t = tempMatrix.row_t;
            col_t = tempMatrix.col_t;
            val_t = tempMatrix.val_t;

            // compression size
            compression_size = tempMatrix.compression_size;

            // void pointers for compression
            comp_ptr = tempMatrix.comp_ptr;
            begin_ptr = tempMatrix.begin_ptr;

            // Free memory
            delete[] vals_arr;
            delete[] indexes_arr;
            delete[] col_p_arr;
        }

        // ? could potentially move some logic outside the constructor for performance gains ?
        /* CSC Constructor
        Takes in 3 arrays of a CSC sparse matrix as well as the dimensions and destructively compresses the sparse data to SRLE
        - Can ask constructor to non-destructively compress the data
        */
        template <typename values_t, typename row_ind, typename col_ind>
        SparseMatrix(values_t** vals, row_ind** indexes, col_ind** col_p,
                     size_t non_zeros, size_t row_num, size_t col_num,
                     bool destroy = true, int compression_level = 3) {

            // ! Destructive Method

            // Initialize data
            num_rows = row_num;
            num_cols = col_num;
            num_nonzeros = non_zeros;

            row_t = byte_width(num_rows);
            col_t = byte_width(num_cols);
            val_t = sizeof(values_t);

            // Allocate memory for compression
            allocate_memory();

            // !! Debug ptr
            void* debug = begin_ptr;

            // Construct Metadata --------------------
            // * <row_t, col_t, val_t, num_rows, num_cols, [col_pointers], {...runs...}>

            // Row, Col, and Val sizes onto compression
            *(uint32_t*)(comp_ptr) = row_t;
            comp_ptr = (uint32_t*)(comp_ptr)+1;

            *(uint32_t*)(comp_ptr) = col_t;
            comp_ptr = (uint32_t*)(comp_ptr)+1;

            *(uint32_t*)(comp_ptr) = val_t;
            comp_ptr = (uint32_t*)(comp_ptr)+1;

            // Number of Rows and Cols onto compression
            *(uint32_t*)(comp_ptr) = num_rows;
            comp_ptr = (uint32_t*)(comp_ptr)+1;

            *(uint32_t*)(comp_ptr) = num_cols;
            comp_ptr = (uint32_t*)(comp_ptr)+1;

            // Create a space for col pointers
            // ! currently size uint64_t and positive delta encoded
            uint64_t* col_pointers = (uint64_t*)(comp_ptr);
            comp_ptr = (uint64_t*)(comp_ptr)+(uint64_t)(num_cols);

            // Put a delim at the end of the metadata
            *(uint32_t*)(comp_ptr) = delim;
            comp_ptr = (uint32_t*)(comp_ptr)+1;
            // End of Metadata --------------------

            // Loop through each column and construct the compression runs
            for (size_t i = 0; i < num_cols; i++) {

                // Update the col pointer
                col_pointers[i] = (uint64_t)(comp_ptr)-(uint64_t)(begin_ptr);
                // std::cout << std::distance((uint8_t *)(begin_ptr), (uint8_t *)(comp_ptr)) << std::endl;

                // For each element in the column check if it's a new value
                for (size_t j = (*col_p)[i]; j < (*col_p)[i + 1]; j++) {

                    // New unique value check
                    if ((*vals)[j] != 0) {
                        // New unique value found

                        // Add the found value to run
                        *(values_t*)(comp_ptr) = (*vals)[j];
                        comp_ptr = (uint32_t*)(comp_ptr)+1;

                        // Create an index pointer to update index type later
                        void* help_ptr = comp_ptr;

                        // default index type to row index type and iterate pointer
                        *(uint8_t*)help_ptr = (uint8_t)sizeof(row_ind);
                        comp_ptr = (uint8_t*)(comp_ptr)+1;

                        // Add the found index to run
                        *(row_ind*)(comp_ptr) = (*indexes)[j];
                        comp_ptr = (uint32_t*)(comp_ptr)+1;

                        // Loop through rest of column to get rest of indices
                        for (size_t k = j + 1; k < (*col_p)[i + 1]; k++) {

                            if ((*vals)[k] == (*vals)[j]) {

                                // Found value again

                                // add index of value to run
                                *(row_ind*)(comp_ptr) = (*indexes)[k];
                                comp_ptr = (row_ind*)(comp_ptr)+1;

                                // set value to zero
                                (*vals)[k] = 0;
                            }
                        }

                        // Set first index found to 0
                        (*vals)[j] = 0;

                        //* Positive delta encode the indices

                        // set variable for max element
                        size_t max_index = 0;

                        // find number of elements found for unique value
                        size_t num_elements = (row_ind*)(comp_ptr)-((row_ind*)(help_ptr));

                        // bring comp_ptr back to being pointed at last found index
                        comp_ptr = (row_ind*)(comp_ptr)-1;

                        // loop moves comp_ptr backwards through indices and positive delta encodes them
                        for (size_t k = 0; k < num_elements - 1; k++) {

                            // subtract element from one before it
                            *(row_ind*)(comp_ptr) = *(row_ind*)(comp_ptr)-*((row_ind*)(comp_ptr)-1);

                            // if bigger then prev max make curr max idx
                            if (*(row_ind*)(comp_ptr) > max_index) {
                                max_index = *(row_ind*)(comp_ptr);
                            }

                            comp_ptr = (row_ind*)(comp_ptr)-1; // loop control
                        }

                        // set index pointer to correct size for run
                        *(uint8_t*)(help_ptr) = byte_width(max_index);
                        help_ptr = (uint8_t*)(help_ptr)+1;

                        // write over data with indices of new size, index compression
                        switch (byte_width(max_index)) {
                        case 1:

                            // walk the two iterators, compressing down to optimal byte width
                            for (size_t k = 0; k < num_elements; k++) {

                                // set index to uint8_t size
                                *(uint8_t*)(comp_ptr) = (uint8_t) * (row_ind*)(help_ptr);

                                // Iterate pointers
                                comp_ptr = (uint8_t*)(comp_ptr)+1;
                                help_ptr = (row_ind*)(help_ptr)+1;
                            }

                            // Add delim
                            *(uint8_t*)(comp_ptr) = delim;
                            comp_ptr = (uint8_t*)(comp_ptr)+1;

                            break;

                        case 2:
                            // walk the two iterators, compressing down to optimal byte width
                            for (size_t k = 0; k < num_elements; k++) {

                                // set index to uint16_t size
                                *(uint16_t*)(comp_ptr) = (uint16_t) * (row_ind*)(help_ptr);

                                // Iterate pointers
                                comp_ptr = (uint16_t*)(comp_ptr)+1;
                                help_ptr = (row_ind*)(help_ptr)+1;
                            }

                            // Add delim
                            *(uint16_t*)(comp_ptr) = delim;
                            comp_ptr = (uint16_t*)(comp_ptr)+1;

                            break;

                        case 4:
                            // walk the two iterators, compressing down to optimal byte width
                            for (size_t k = 0; k < num_elements; k++) {

                                // set index to uint8_t size
                                *(uint32_t*)(comp_ptr) = (uint32_t) * (row_ind*)(help_ptr);

                                // Iterate pointers
                                comp_ptr = (uint32_t*)(comp_ptr)+1;
                                help_ptr = (row_ind*)(help_ptr)+1;
                            }

                            // Add delim
                            *(uint32_t*)(comp_ptr) = delim;
                            comp_ptr = (uint32_t*)(comp_ptr)+1;

                            break;

                        } // end switch

                        help_ptr = comp_ptr;
                    } // end if

                } // end for loop of uniques in col

            } // end of col for loop

            // remove ending zeros
            while (comp_ptr != begin_ptr && *(uint8_t*)(comp_ptr) == 0) {
                comp_ptr = (uint8_t*)(comp_ptr)-1;
            }

            // positive delta encode the column pointers
            for (size_t i = num_cols - 1; i > 0; i--) {
                col_pointers[i] = col_pointers[i] - col_pointers[i - 1];
            }

            // find size of file in bytes
            compression_size = (uint8_t*)(comp_ptr)-((uint8_t*)(begin_ptr)-1);

            // resize data to fit actual size
            begin_ptr = realloc(begin_ptr, compression_size);

            // ! write data to file
            FILE* fp = fopen("data.bin", "wb");
            fwrite(begin_ptr, 1, compression_size, fp);
            fclose(fp);

        } // end of constructor

        //     ~SparseMatrix()
        //    {
        //         if(is_allocated)
        //             free(begin_ptr);
        //         is_allocated = false;
        //    }

        size_t getSize() const {
            return compression_size;
        }

        void* getData() {
            return begin_ptr;
        }

        void* getEnd() {
            return comp_ptr;
        }

    }; // end of SparseMatrix class

    // Iterator class
    template <typename T>
    class iterator {

    private:
        uint64_t index = 0;
        uint64_t* colPointers;
        uint32_t valueWidth;
        uint8_t newIndexWidth;
        char* fileData;
        void* endOfData;
        void* currentIndex;
        T value;
        bool firstIndex = true;

        void readColumnPointers() {

            memcpy(&colPointers, currentIndex, interpretPointer(8));

            for (int i = 0; i < interpretPointer(8); i++) {
                cout << colPointers[i] << endl;
            }
        }

    public:
        /**
         * @brief Construct a new CSFiterator object
         *
         * @param filePath
         */

        iterator(CSF::SparseMatrix& matrix) {
            currentIndex = matrix.getData();
            endOfData = matrix.getEnd();

            // read first 28 bytes of fileData put it into params -> metadata
            uint32_t params[5];
            cout << "Flag 0" << endl;
            memcpy(&params, currentIndex, 20);
            cout << "Flag 1" << endl;
            currentIndex = static_cast<char*>(currentIndex) + 20;

            // valueWidth is set and the first value is read in
            valueWidth = params[4];
            value = interpretPointer(valueWidth);

            readColumnPointers();

            // Read in the width of this run's indices and go to first index
            newIndexWidth = *static_cast<uint8_t*>(currentIndex);
            currentIndex = static_cast<char*>(currentIndex) + 1;

            // cout << "value: " << value << endl;
            // cout << "newIndexWidth: " << (int)newIndexWidth << endl;
        }

        iterator(const char* filePath) {
            readFile(filePath);

            // read first 28 bytes of fileData put it into params -> metadata
            uint32_t params[8];

            memcpy(&params, currentIndex, 24);
            currentIndex = static_cast<char*>(currentIndex) + 24;

            // valueWidth is set and the first value is read in
            valueWidth = params[4];
            value = interpretPointer(valueWidth);

            // Read in the width of this run's indices and go to first index
            newIndexWidth = *static_cast<uint8_t*>(currentIndex);
            currentIndex = static_cast<char*>(currentIndex) + 1;

            // cout << "value: " << value << endl;
            // cout << "newIndexWidth: " << (int)newIndexWidth << endl;
        }

        /**
         * @brief Returns the value of the run.
         *
         * @return T&
         */

        T& operator*() { return value; };

        /**
         * @brief Increment the iterator
         *
         * @return uint64_t
         */

        uint64_t operator++() {
            uint64_t newIndex = interpretPointer(newIndexWidth);

            // cout << "newIndex: " << newIndex << endl;
            // cout << "width: " << (int)newIndexWidth << endl;
            // cout << "value " << value << endl << endl;

            // If newIndex is 0 and not the first index, then the index is a delimitor
            if (newIndex == 0 && !firstIndex) {
                // Value is the first index of the run
                value = interpretPointer(valueWidth);

                // newIndexWidth is the second value in the run
                newIndexWidth = *static_cast<uint8_t*>(currentIndex);
                currentIndex = static_cast<char*>(currentIndex) + 1;

                memset(&index, 0, 8);

                // cout << "value2 " << value << endl;
                // cout << "width2: " << (int)newIndexWidth << endl;

                // Returns the first index of the run
                index = interpretPointer(newIndexWidth);
                firstIndex = true;
                return index;
            }

            // Returns the next index of the run for positive delta encoded runs
            firstIndex = false;
            return index += newIndex;
        }

        /**
         * @brief Check if the iterator is at the end of the the data
         *
         * @return true
         * @return false
         */

        operator bool() { return endOfData != currentIndex; }

    private:
        /**
         * @brief Read a file into memory
         *
         * @param filePath
         */

        inline void readFile(const char* filePath) {
            FILE* file = fopen(filePath, "rb");

            // Find end of file and allocate size
            fseek(file, 0, SEEK_END);
            int sizeOfFile = ftell(file);
            fileData = (char*)malloc(sizeof(char*) * sizeOfFile);

            // Read file into memory
            fseek(file, 0, SEEK_SET);
            fread(fileData, sizeOfFile, 1, file);
            fclose(file);

            currentIndex = fileData;
            endOfData = fileData + sizeOfFile;
        }

        /**
         * @brief Read in the next index from the file based on a variable width
         *
         * @param width
         * @return uint64_t
         */

        inline uint64_t interpretPointer(int width) {
            uint64_t newIndex = 0;

            // Case statement takes in 1,2,4, or 8 otherwise the width is invalid
            switch (width) {
            case 1:
                newIndex = static_cast<uint64_t>(*static_cast<uint8_t*>(currentIndex));
                break;
            case 2:
                newIndex = static_cast<uint64_t>(*static_cast<uint16_t*>(currentIndex));
                break;
            case 4:
                newIndex = static_cast<uint64_t>(*static_cast<uint32_t*>(currentIndex));
                break;
            case 8:
                newIndex = static_cast<uint64_t>(*static_cast<uint64_t*>(currentIndex));
                break;
            default:
                // cout << static_cast<int>(*static_cast<uint8_t*>(currentIndex)) << endl;
                cout << "Invalid width: " << width << endl;
                exit(-1);
                break;
            }

            currentIndex = static_cast<char*>(currentIndex) + width;
            return newIndex;
        }
    };

}; // end of namespace