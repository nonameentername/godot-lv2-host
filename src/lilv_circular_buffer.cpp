#include "lilv_circular_buffer.h"

using namespace godot;

template <typename T>
LilvCircularBuffer<T>::LilvCircularBuffer() {
    audio_buffer = new AudioRingBuffer<T>();
}

template <typename T>
LilvCircularBuffer<T>::~LilvCircularBuffer() {
    delete audio_buffer;
}

template <typename T>
void LilvCircularBuffer<T>::write_channel(const T *p_buffer, int p_frames) {
    for (int frame = 0; frame < p_frames; frame++) {
        audio_buffer->buffer[(audio_buffer->write_index + frame) % CIRCULAR_BUFFER_SIZE] = p_buffer[frame];
    }
    audio_buffer->write_index = (audio_buffer->write_index + p_frames) % CIRCULAR_BUFFER_SIZE;
}

template <typename T>
int LilvCircularBuffer<T>::read_channel(T *p_buffer, int p_frames) {
    const int read_index  = audio_buffer->read_index;
    const int write_index = audio_buffer->write_index;

	int available = 0;

	if (write_index >= read_index) {
		available = write_index - read_index;
	} else {
		available = (CIRCULAR_BUFFER_SIZE - read_index) + write_index;
	}

    if (available < p_frames) {
        return 0;
    }

    for (int frame = 0; frame < p_frames; frame++) {
        p_buffer[frame] = audio_buffer->buffer[(read_index + frame) % CIRCULAR_BUFFER_SIZE];
    }

	return p_frames;
}

template <typename T>
void LilvCircularBuffer<T>::update_read_index(int p_frames) {
    audio_buffer->read_index = (audio_buffer->read_index + p_frames) % CIRCULAR_BUFFER_SIZE;
}

namespace godot {
	template class LilvCircularBuffer<float>;
	template class LilvCircularBuffer<int>;
}
