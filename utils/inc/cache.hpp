#ifndef __CACHE_HPP__
#define __CACHE_HPP__

#include <map>

namespace utils {

template <typename K, typename V>
class Cache
{
private:
    std::map<K, V> cache_;

public:
    bool hit(const K &k);
    bool get(const K &k, V &v);
    void update(const K &k, const V &v);
    void remove(const K &k);
};

template <typename K, typename V>
bool Cache<K, V>::hit(const K &k)
{
    typename std::map<K, V>::iterator it;
    it = cache_.find(k);
    return !(it == cache_.end());
}

template <typename K, typename V>
bool Cache<K, V>::get(const K &k, V &v)
{
    
    typename std::map<K, V>::iterator it;
    if ((it = cache_.find(k)) == cache_.end()) {
        return false;
    }
    v = it->second;
    return true;
}

template <typename K, typename V>
void Cache<K, V>::update(const K &k, const V &v)
{
    cache_[k] = v;
}

template <typename K, typename V>
void Cache<K, V>::remove(const K &k)
{
    
    typename std::map<K, V>::iterator it;
    if ((it = cache_.find(k)) != cache_.end()) {
        cache_remove(it);
    }
}

}

#endif

