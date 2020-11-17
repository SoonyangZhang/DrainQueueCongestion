#pragma once
#include <deque>
#include "proto_types.h"
#include "logging.h"
namespace dqc{
template <typename T>
class PacketNumberIndexedQueue {
 public:
  PacketNumberIndexedQueue() : number_of_present_entries_(0) {}

  // Retrieve the entry associated with the packet number.  Returns the pointer
  // to the entry in case of success, or nullptr if the entry does not exist.
  T* GetEntry(QuicPacketNumber packet_number);
  const T* GetEntry(QuicPacketNumber packet_number) const;

  // Inserts data associated |packet_number| into (or past) the end of the
  // queue, filling up the missing intermediate entries as necessary.  Returns
  // true if the element has been inserted successfully, false if it was already
  // in the queue or inserted out of order.
  template <typename... Args>
  bool Emplace(QuicPacketNumber packet_number, Args&&... args);

  // Removes data associated with |packet_number| and frees the slots in the
  // queue as necessary.
  bool Remove(QuicPacketNumber packet_number);

  // Same as above, but if an entry is present in the queue, also call f(entry)
  // before removing it.
  template <typename Function>
  bool Remove(QuicPacketNumber packet_number, Function f);

  // Remove up to, but not including |packet_number|.
  // Unused slots in the front are also removed, which means when the function
  // returns, |first_packet()| can be larger than |packet_number|.
  void RemoveUpTo(QuicPacketNumber packet_number);
  bool IsEmpty() const { return number_of_present_entries_ == 0; }

  // Returns the number of entries in the queue.
  size_t number_of_present_entries() const {
    return number_of_present_entries_;
  }

  // Returns the number of entries allocated in the underlying deque.  This is
  // proportional to the memory usage of the queue.
  size_t entry_slots_used() const { return entries_.size(); }

  // Packet number of the first entry in the queue.
  QuicPacketNumber first_packet() const { return first_packet_; }

  // Packet number of the last entry ever inserted in the queue.  Note that the
  // entry in question may have already been removed.  Zero if the queue is
  // empty.
  QuicPacketNumber last_packet() const {
    if (IsEmpty()) {
      return QuicPacketNumber();
    }
    return first_packet_ + entries_.size() - 1;
  }

 private:
  // Wrapper around T used to mark whether the entry is actually in the map.
  struct EntryWrapper : T {
    bool present;

    EntryWrapper() : present(false) {}

    template <typename... Args>
    explicit EntryWrapper(Args&&... args)
        : T(std::forward<Args>(args)...), present(true) {}
  };

  // Cleans up unused slots in the front after removing an element.
  void Cleanup();

  const EntryWrapper* GetEntryWrapper(QuicPacketNumber offset) const;
  EntryWrapper* GetEntryWrapper(QuicPacketNumber offset) {
    const auto* const_this = this;
    return const_cast<EntryWrapper*>(const_this->GetEntryWrapper(offset));
  }

  std::deque<EntryWrapper> entries_;
  size_t number_of_present_entries_;
  QuicPacketNumber first_packet_;
};

template <typename T>
T* PacketNumberIndexedQueue<T>::GetEntry(QuicPacketNumber packet_number) {
  EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry;
}

template <typename T>
const T* PacketNumberIndexedQueue<T>::GetEntry(
    QuicPacketNumber packet_number) const {
  const EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry;
}

template <typename T>
template <typename... Args>
bool PacketNumberIndexedQueue<T>::Emplace(QuicPacketNumber packet_number,
                                          Args&&... args) {
  if (!packet_number.IsInitialized()) {
    DLOG(FATAL)<< "Try to insert an uninitialized packet number";
    return false;
  }

  if (IsEmpty()) {
    DCHECK(entries_.empty());
    DCHECK(!first_packet_.IsInitialized());

    entries_.emplace_back(std::forward<Args>(args)...);
    number_of_present_entries_ = 1;
    first_packet_ = packet_number;
    return true;
  }

  // Do not allow insertion out-of-order.
  if (packet_number <= last_packet()) {
    return false;
  }

  // Handle potentially missing elements.
  size_t offset = packet_number - first_packet_;
  if (offset > entries_.size()) {
    entries_.resize(offset);
  }

  number_of_present_entries_++;
  entries_.emplace_back(std::forward<Args>(args)...);
  DCHECK_EQ(packet_number, last_packet());
  return true;
}

template <typename T>
bool PacketNumberIndexedQueue<T>::Remove(QuicPacketNumber packet_number) {
  return Remove(packet_number, [](const T&) {});
}

template <typename T>
template <typename Function>
bool PacketNumberIndexedQueue<T>::Remove(QuicPacketNumber packet_number,
                                         Function f) {
  EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return false;
  }
  f(*static_cast<const T*>(entry));
  entry->present = false;
  number_of_present_entries_--;

  if (packet_number == first_packet()) {
    Cleanup();
  }
  return true;
}
template <typename T>
void PacketNumberIndexedQueue<T>::RemoveUpTo(QuicPacketNumber packet_number) {
  while (!entries_.empty() && first_packet_.IsInitialized() &&
         first_packet_ < packet_number) {
    if (entries_.front().present) {
      number_of_present_entries_--;
    }
    entries_.pop_front();
    first_packet_++;
  }
  Cleanup();
}
template <typename T>
void PacketNumberIndexedQueue<T>::Cleanup() {
  while (!entries_.empty() && !entries_.front().present) {
    entries_.pop_front();
    first_packet_++;
  }
  if (entries_.empty()) {
    first_packet_.Clear();
  }
}

template <typename T>
auto PacketNumberIndexedQueue<T>::GetEntryWrapper(
    QuicPacketNumber packet_number) const -> const EntryWrapper* {
  if (!packet_number.IsInitialized() || IsEmpty() ||
      packet_number < first_packet_) {
    return nullptr;
  }

  uint64_t offset = packet_number - first_packet_;
  if (offset >= entries_.size()) {
    return nullptr;
  }

  const EntryWrapper* entry = &entries_[offset];
  if (!entry->present) {
    return nullptr;
  }

  return entry;
}

}
