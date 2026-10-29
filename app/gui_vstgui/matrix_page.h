#pragma once

#include <algorithm>
#include <cstddef>

namespace sar::gui_vstgui {

// Page indexes are retained across temporary disconnects, but must be clamped
// when a new authoritative topology is received.
class MatrixPage {
 public:
  explicit MatrixPage(std::size_t capacity) : capacity_((std::max)(capacity, std::size_t{1})) {}

  void reconcile(std::size_t count) {
    count_ = count;
    page_ = (std::min)(page_, count == 0 ? std::size_t{0} : (count - 1) / capacity_);
  }
  bool previous() {
    if (!hasPrevious()) return false;
    --page_;
    return true;
  }
  bool next() {
    if (!hasNext()) return false;
    ++page_;
    return true;
  }
  bool hasPrevious() const { return page_ != 0; }
  bool hasNext() const { return count_ != 0 && page_ < (count_ - 1) / capacity_; }
  std::size_t first() const { return page_ * capacity_; }
  std::size_t page() const { return page_; }
  std::size_t visibleCount() const { return (std::min)(capacity_, count_ - first()); }
  bool contains(std::size_t slot) const { return slot < visibleCount(); }

 private:
  std::size_t capacity_;
  std::size_t count_ = 0;
  std::size_t page_ = 0;
};

}  // namespace sar::gui_vstgui
