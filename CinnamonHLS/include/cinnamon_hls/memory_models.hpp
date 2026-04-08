#ifndef CINNAMON_HLS_MEMORY_MODELS_HPP_
#define CINNAMON_HLS_MEMORY_MODELS_HPP_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cinnamon_hls {

class RectMemModel {
 public:
  RectMemModel(std::size_t rows, std::size_t cols)
      : rows_(rows), cols_(cols), mem_(cols, std::vector<std::uint64_t>(rows, 0)),
        read_addr_reg_(0) {}

  void write(std::size_t col, const std::vector<std::uint64_t> &row_values,
             bool enable) {
    if (!enable) {
      return;
    }
    check_col(col);
    if (row_values.size() != rows_) {
      throw std::invalid_argument("row_values size mismatch");
    }
    mem_[col] = row_values;
  }

  void set_read_addr(std::size_t col) {
    check_col(col);
    next_read_addr_ = col;
  }

  void tick() { read_addr_reg_ = next_read_addr_; }

  std::vector<std::uint64_t> read_data() const { return mem_.at(read_addr_reg_); }

 private:
  void check_col(std::size_t col) const {
    if (col >= cols_) {
      throw std::out_of_range("column out of range");
    }
  }

  std::size_t rows_;
  std::size_t cols_;
  std::vector<std::vector<std::uint64_t>> mem_;
  std::size_t read_addr_reg_;
  std::size_t next_read_addr_ = 0;
};

class RegfileModel {
 public:
  RegfileModel(std::size_t row_size, std::size_t reg_num)
      : row_size_(row_size), reg_num_(reg_num),
        mem_(reg_num, std::vector<std::uint64_t>(row_size, 0)) {}

  void write(std::size_t reg_addr, const std::vector<std::uint64_t> &row_data,
             bool enable) {
    if (!enable) {
      return;
    }
    check_reg(reg_addr);
    if (row_data.size() != row_size_) {
      throw std::invalid_argument("row_data size mismatch");
    }
    mem_[reg_addr] = row_data;
  }

  void set_read(std::size_t port_id, std::size_t reg_addr) {
    if (port_id >= read_addr_next_.size()) {
      read_addr_next_.resize(port_id + 1, 0);
      read_addr_reg_.resize(port_id + 1, 0);
    }
    check_reg(reg_addr);
    read_addr_next_[port_id] = reg_addr;
  }

  void tick() {
    if (read_addr_reg_.size() < read_addr_next_.size()) {
      read_addr_reg_.resize(read_addr_next_.size(), 0);
    }
    read_addr_reg_ = read_addr_next_;
  }

  std::vector<std::uint64_t> read(std::size_t port_id) const {
    if (port_id >= read_addr_reg_.size()) {
      throw std::out_of_range("port out of range");
    }
    return mem_.at(read_addr_reg_[port_id]);
  }

 private:
  void check_reg(std::size_t reg_addr) const {
    if (reg_addr >= reg_num_) {
      throw std::out_of_range("register out of range");
    }
  }

  std::size_t row_size_;
  std::size_t reg_num_;
  std::vector<std::vector<std::uint64_t>> mem_;
  std::vector<std::size_t> read_addr_next_;
  std::vector<std::size_t> read_addr_reg_;
};

}  // namespace cinnamon_hls

#endif  // CINNAMON_HLS_MEMORY_MODELS_HPP_
