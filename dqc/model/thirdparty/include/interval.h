#pragma once
#include "proto_types.h"
#include <iostream>
#include <utility>
#include <set>
template<typename T>
class Interval{
private:
  // Type trait for deriving the return type for QuicInterval::Length.  If
  // operator-() is not defined for T, then the return type is void.  This makes
  // the signature for Length compile so that the class can be used for such T,
  // but code that calls Length would still generate a compilation error.
  template <typename U>
  class DiffTypeOrVoid {
   private:
    template <typename V>
    static auto f(const V* v) -> decltype(*v - *v);
    template <typename V>
    static void f(...);

   public:
    using type = typename std::decay<decltype(f<U>(nullptr))>::type;
  };
public:
typedef Interval<T> value_type;
Interval():min_(),max_(){}
Interval(const T &min,const T & max):min_(min),max_(max){}
void SetMin(const T &min){
    min_=min;
}
void SetMax(const T &max){
    max_=max;
}
bool Contains(const T &t) const{
    return ((min_<=t)&&(t<max_));
}
bool Contains(const Interval<T> &i) const{
    return Min()<=i.Min()&&Max()>=i.Max();
}
const T&Max() const{
    return max_;
}
const T&Min() const{
    return min_;
}
const T&max() const{
    return max_;
}
const T&min() const{
    return min_;
}
bool Empty() const{
    return Min()>=Max();
}
typename DiffTypeOrVoid<T>::type Length() const{
    return (Empty()?Min():Max())-Min();
}
Interval<T>& operator=(const value_type &o){
    min_=o.Min();
    max_=o.Max();
    return (*this);
}
friend bool operator==(const value_type &a,const value_type &b){
    return (a.Min()==b.Min())&&(a.Max()==b.Max());
}
private:
    T min_;
    T max_;
};
template<class T>
class IntervalSet{
public:
    typedef Interval<T> value_type;
private:
    struct IntervalLess{
        bool operator()(const value_type &a,const value_type &b) const;
    };
    typedef typename std::set<Interval<T>,IntervalLess> Set;
public:
    typedef typename Set::iterator iterator;
    typedef typename Set::const_iterator const_iterator;
    IntervalSet(){}
    ~IntervalSet(){}
    const_iterator begin() const {
        return intervals_.begin();
    }
    const_iterator end()  const{
        return intervals_.end();
    }
    void Clear(){
        intervals_.clear();
    }
    size_t Size() const{
        return intervals_.size();
    }
    bool Empty() const{
        return intervals_.empty();
    }
    void Swap(IntervalSet<T> *o){
        intervals_.swap(o->intervals_);
    }
    void Add(const T&min,const T&max){
        Add(value_type(min,max));
    }
    void Add(const value_type&interval);
    bool Contain(const T &min,const T &max) const{
        return Contain(value_type(min,max));
    }
    bool Contain(const value_type&interval) const;
    bool IsDisjoint(const T&min,const T&max) const{
        return IsDisjoint(value_type(min,max));
    }
    bool IsDisjoint(const value_type&interval) const;
  // Returns an iterator pointing to the first value_type which goes after
  // the given value.
  //
  // Example:
  //   [10, 20)  [30, 40)
  //             ^          UpperBound(10)
  //             ^          UpperBound(15)
  //             ^          UpperBound(20)
  //             ^          UpperBound(25)
    const_iterator UpperBound(const T&value) const{
        return intervals_.upper_bound(value_type(value,value));
    }
  // Returns an iterator pointing to the first value_type which contains or
  // goes after the given value.
  //
  // Example:
  //   [10, 20)  [30, 40)
  //   ^                    LowerBound(10)
  //   ^                    LowerBound(15)
  //             ^          LowerBound(20)
  //             ^          LowerBound(25)
    const_iterator LowerBound(const T& value) const{
        return intervals_.lower_bound(value_type(value,value));
    }
    void Test(const T &min,const T &max);

private:
    void Compact(iterator begin,iterator end);
private:
    Set intervals_;
};
//Be careful. the value waiting for insert.
//a.Max()>b.Max()  for Contain
//I found the memslice can not be free due to contain.
template<class T>
bool IntervalSet<T>::IntervalLess::operator()(const value_type &a,
                                              const value_type &b) const
{
    return a.Min()<b.Min()||(a.Min()==b.Min()&&a.Max()>b.Max());
}
template<class T>
void IntervalSet<T>::Add(const value_type&interval){
     std::pair<iterator,bool> ins=intervals_.insert(interval);
     if(!ins.second){
        return;
     }
     iterator begin=ins.first;
     if(begin!=intervals_.begin()){
        begin--;
     }
     iterator end=intervals_.end();
     Compact(begin,end);
}
template<class T>
void IntervalSet<T>::Test(const T &min,const T &max){
    //iterator it=intervals_.upper_bound(interval);
}
template<class T>
void IntervalSet<T>::Compact(iterator begin,iterator end){
    if(begin==end){
        return;
    }
    iterator it=begin;
    iterator prev=begin;
    iterator next=begin;
    ++next;
    ++it;
    while(it!=end){
        ++next;
        if(prev->Max()>=it->Min()){
            T min=prev->Min();
            T max=std::max(prev->Max(),it->Max());
            intervals_.erase(prev);
            intervals_.erase(it);
            std::pair<iterator,bool> ins=intervals_.insert(value_type(min,max));
            prev=ins.first;
        }else{
            prev=it;
        }
        it=next;
    }
}
template<class T>
bool IntervalSet<T>::Contain(const value_type&interval) const{
    iterator it=intervals_.upper_bound(interval);
    if(it==intervals_.begin()){
      return false;
    }
    it--;
    return it->Contains(interval);
}
template<class T>
bool IntervalSet<T>::IsDisjoint(const value_type&interval) const{
  if (interval.Empty())
    return true;
  value_type tmp(interval.min(), interval.min());
  // Find the first interval with min() > interval.min()
  const_iterator it = intervals_.upper_bound(tmp);
  if (it != intervals_.end() && interval.max() > it->min())
    return false;
  if (it == intervals_.begin())
    return true;
  --it;
  return it->max() <= interval.min();
}
