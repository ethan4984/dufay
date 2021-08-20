#ifndef VECTOR_HPP_
#define VECTOR_HPP_

#include <mm/slab.hpp>
#include <memutils.hpp>
#include <utility>
#include <debug.hpp>

namespace lib {

template <typename T>
class vector {
public:
    vector(const vector &other);
    vector(vector &&other);
    vector();
    ~vector();

    vector &operator= (vector other) {
        element_cnt = other.element_cnt;
        buf_cap = other.buf_cap;
        elements = new T[buf_cap];

        for(size_t i = 0; i < element_cnt; i++) {
            new (&elements[i]) T(other[i]);
        }

        return *this;
    }

    const T &operator[] (size_t index) const {
        return elements[index];
    }

    T &operator[] (size_t index) {
        return elements[index];
    }

    T &push(const T &elem);
    T &push(T &&elem);
    T &last() { return elements[element_cnt - 1]; }
    T pop();

    void remove(size_t index);

    T *data() { return elements; }
    T *begin() { return elements; }
    const T *begin() const { return elements; }
    T *end() { return elements + element_cnt; }
    bool empty() const { return element_cnt == 0; }
    const T *end() const { return elements + element_cnt; }
    size_t size() const { return element_cnt; }
private:
    void internal_resize(size_t cap);
    void swap(vector &a, vector &b);

    T *elements;
    size_t element_cnt;  
    size_t buf_cap;
};

template <typename T>
void vector<T>::swap(vector &a, vector &b) {
    std::swap(a.elements, b.elements);
    std::swap(a.element_cnt, b.element_cnt);
    std::swap(a.buf_cap, b.buf_cap);
}

template <typename T>
vector<T>::vector(const vector &other) : elements(NULL), element_cnt(0), buf_cap(0) {
    element_cnt = other.element_cnt;
    buf_cap = other.buf_cap;
    elements = new T[buf_cap];

    for(size_t i = 0; i < element_cnt; i++) {
        new (&elements[i]) T(other[i]);
    }
}

template <typename T>
vector<T>::vector(vector &&other) : elements(NULL), element_cnt(0), buf_cap(0) {
    swap(*this, other);
}

template <typename T>
vector<T>::vector() : elements(NULL), element_cnt(0), buf_cap(0) { }

template <typename T>
vector<T>::~vector() {
    for(size_t i = 0; i < element_cnt; i++) {
        elements[i].~T();
    }

    delete [] elements;
}

template <typename T>
T &vector<T>::push(const T &element) {
    internal_resize(element_cnt + 1);
    return *(new (&elements[element_cnt++]) T(std::move(element)));
}

template <typename T>
T &vector<T>::push(T &&element) {
    internal_resize(element_cnt + 1);
    return *(new (&elements[element_cnt++]) T(std::move(element)));
}

template <typename T>
T vector<T>::pop() {
    T element = std::move(elements[--element_cnt]);
    elements[element_cnt].~T();
    return element;
}

template <typename T>
void vector<T>::internal_resize(size_t cap) {
    if(cap <= buf_cap)
        return;

    size_t new_cap = cap * 2;
    T *new_arr = new T[new_cap];

    for(size_t i = 0; i < element_cnt; i++) 
        new (&new_arr[i]) T(std::move(elements[i]));

    for(size_t i = 0; i < element_cnt; i++)
        elements[i].~T();

    delete [] elements; 

    elements = new_arr;
    buf_cap = new_cap;
}

template <typename T>
void vector<T>::remove(size_t index) {
    if(element_cnt <= index) 
        return;

    size_t origin_cnt = 0;
    T *new_arr = new T[element_cnt - 1];

    for(size_t i = 0; i < index; i++) {
        new (&new_arr[origin_cnt++]) T(std::move(elements[i]));
    }

    for(size_t i = index + 1; i < element_cnt; i++) {
        new (&new_arr[origin_cnt++]) T(std::move(elements[i]));
    }

    if(elements)
        delete [] elements;

    elements = new_arr;

    element_cnt--;
    buf_cap = element_cnt;
}

}

#endif
