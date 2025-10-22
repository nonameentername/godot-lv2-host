#ifndef LILV_CIRCULAR_BUFFER_H
#define LILV_CIRCULAR_BUFFER_H

namespace godot {

const int CIRCULAR_BUFFER_SIZE = 2048;

template <typename T>
struct AudioRingBuffer {
    int write_index = 0;
    int read_index = 0;

    T buffer[CIRCULAR_BUFFER_SIZE];
};


template <typename T>
class LilvCircularBuffer {

private:
    AudioRingBuffer<T> *audio_buffer;

public:
    LilvCircularBuffer();
    ~LilvCircularBuffer();

	//TODO: rename to write_buffer or write
    void write_channel(const T *p_buffer, int p_frames);

	//TODO: rename to read_buffer or read
    int read_channel(T *p_buffer, int p_frames);

    void update_read_index(int p_frames);
};

} // namespace godot

#endif
