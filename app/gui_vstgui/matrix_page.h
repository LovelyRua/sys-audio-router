#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>

namespace sar::gui_vstgui {

inline std::optional<std::size_t> adjacentMatrixCell(
    std::size_t index, std::size_t rows, std::size_t columns,
    int deltaRow, int deltaColumn) {
  if (!rows || !columns || index / columns >= rows) return std::nullopt;
  auto row = index / columns;
  auto column = index % columns;
  if (deltaRow == -1 && deltaColumn == 0 && row > 0) --row;
  else if (deltaRow == 1 && deltaColumn == 0 && row < rows - 1) ++row;
  else if (deltaColumn == -1 && deltaRow == 0 && column > 0) --column;
  else if (deltaColumn == 1 && deltaRow == 0 && column < columns - 1) ++column;
  else return std::nullopt;
  return row * columns + column;
}

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
