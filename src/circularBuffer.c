#include "circularBuffer.h"

int buffer_init(CircularBuffer* buffer, int size)  {
    buffer->data = malloc(size +1 );
    if (buffer->data == NULL) 
        return -1; // there has been an error allocating

    buffer->size = size + 1; // Note: Only N -1 bytes can be stored in this implementation, as it cannot differentiate 
                             // between an empty or full buffer (check wikipedia for more details)
    buffer->start = 0;
    buffer->end = 0;
    return 0;
}

void buffer_deallocate(CircularBuffer* buffer) {
    //deallocate the buffer
    free(buffer->data);
    buffer->size = 0;
}

int buffer_used_bytes(CircularBuffer* buffer) {
    // Returns the number of bytes of the buffer that contain data
    return (buffer->size + buffer->end - buffer->start) % buffer->size;

}

int buffer_free_bytes(CircularBuffer* buffer) {
    // Returns the number of bytes that are free
    return buffer->size - buffer_used_bytes(buffer) -1;

}

int buffer_size_next_element(CircularBuffer* buffer, unsigned char delimeter, int reachedEOF) {
    /*
     * Returns the size (in bytes) of the next fully encoded element
     * currently stored in the circular buffer. The function scans 
     * the buffer looking for the specified delimiter
     * character, which marks the end of an element.
     *
     * Return values:
     *  - If a delimiter is found, the function returns the number of bytes
     *    that form the next complete element (including the delimeter).
     *  - If no delimiter is found and reachedEOF is 0, the function returns -1,
     *    indicating that no complete element is available yet.
     *  - If reachedEOF is 1 and the buffer still contains data, the remaining
     *    bytes in the buffer are considered a complete final element, and
     *    their size is returned.
     */
    int index = buffer->start;
    int count = 0;
    while(buffer->end != index) {
        ++count;
        if (buffer->data[index] == delimeter) {
            return count;
        }
        index = (index + 1)%buffer->size;
    }

    // If we have not found the delimeter:
    //       if we have reached the EOF, then we need to return the number of bytes
    if (reachedEOF && count > 0)
        return count;
    else
        // Otherwise, we need to return -1 to indicate that we need to read again
        return -1;
}

void buffer_push(CircularBuffer* buffer, unsigned char c) {
    // Pushes data to the end of the buffer, without checking whether
    // the buffer allows more data
    buffer->data[buffer->end] = c;
    buffer->end= (buffer->end + 1)%buffer->size;
}

unsigned char buffer_pop(CircularBuffer* buffer) {
    // Pops data from the start of the buffer, without checking 
    // whether the buffer contains data
    unsigned char ret = buffer->data[buffer->start];
    buffer->start= (buffer->start + 1)%buffer->size;
    return ret;
}