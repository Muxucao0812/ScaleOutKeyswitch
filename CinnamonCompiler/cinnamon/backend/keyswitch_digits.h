// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <tuple>

namespace Cinnamon {
namespace Backend {
std::pair<uint16_t, uint16_t>
get_keyswitch_dnum_aggregation_28bit(uint16_t level, uint16_t num_partitions,
                                     uint16_t current_partition_size) {
  if (num_partitions == 1) {
    if (level <= 32) {
      return std::pair<uint16_t, uint16_t>(1, 1);
    } else if (level <= 42) {
      return std::pair<uint16_t, uint16_t>(1, 2);
    } else if (level <= 48) {
      return std::pair<uint16_t, uint16_t>(1, 3);
    } else if (level <= 51) {
      return std::pair<uint16_t, uint16_t>(1, 4);
    } else if (level <= 53) {
      return std::pair<uint16_t, uint16_t>(1, 5);
    } else if (level <= 54) {
      return std::pair<uint16_t, uint16_t>(1, 6);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 7);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 8);
    } else if (level <= 57) {
      return std::pair<uint16_t, uint16_t>(1, 9);
    } else if (level <= 58) {
      return std::pair<uint16_t, uint16_t>(1, 10);
    } else if (level <= 59) {
      return std::pair<uint16_t, uint16_t>(1, 12);
    } else if (level <= 60) {
      return std::pair<uint16_t, uint16_t>(1, 19);
    } else if (level <= 61) {
      return std::pair<uint16_t, uint16_t>(1, 30);
    } else if (level <= 62) {
      return std::pair<uint16_t, uint16_t>(1, 61);
    } else if (level <= 63) {
      return std::pair<uint16_t, uint16_t>(1, 63);
    } else {
      assert(0);
    }
  }

  if (num_partitions == 4) {
    if (current_partition_size == 1) {
      if (level <= 13) {
        return std::pair<uint16_t, uint16_t>(1, 1);
      } else if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(1, 2);
      } else if (level <= 39) {
        return std::pair<uint16_t, uint16_t>(1, 3);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(1, 4);
      } else if (level <= 53) {
        return std::pair<uint16_t, uint16_t>(1, 5);
      } else if (level <= 54) {
        return std::pair<uint16_t, uint16_t>(1, 6);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(1, 7);
      } else if (level <= 57) {
        return std::pair<uint16_t, uint16_t>(1, 9);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(1, 10);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(1, 12);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(1, 19);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(1, 30);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(1, 61);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(1, 63);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 2) {
      if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(2, 1);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(2, 2);
      } else if (level <= 54) {
        return std::pair<uint16_t, uint16_t>(2, 3);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(2, 4);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(2, 5);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(2, 6);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(2, 10);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(2, 15);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(2, 31);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(2, 32);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 4) {
      if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(4, 1);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(4, 2);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(4, 3);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(4, 5);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(4, 8);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(4, 16);
      } else {
        assert(0);
      }
    } else {
      assert(0);
    }
  }

  if (num_partitions == 2) {
    if (current_partition_size == 1) {
      if (level <= 21) {
        return std::pair<uint16_t, uint16_t>(1, 1);
      } else if (level <= 42) {
        return std::pair<uint16_t, uint16_t>(1, 2);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(1, 4);
      } else if (level <= 53) {
        return std::pair<uint16_t, uint16_t>(1, 5);
      } else if (level <= 54) {
        return std::pair<uint16_t, uint16_t>(1, 6);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(1, 7);
      } else if (level <= 57) {
        return std::pair<uint16_t, uint16_t>(1, 9);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(1, 10);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(1, 12);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(1, 19);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(1, 30);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(1, 61);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(1, 63);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 2) {
      if (level <= 42) {
        return std::pair<uint16_t, uint16_t>(2, 1);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(2, 2);
      } else if (level <= 54) {
        return std::pair<uint16_t, uint16_t>(2, 3);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(2, 4);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(2, 5);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(2, 6);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(2, 10);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(2, 15);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(2, 31);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(2, 32);
      } else {
        assert(0);
      }
    } else {
      assert(0);
    }
  }

  if (num_partitions == 8) {
    if (current_partition_size == 1) {
      if (level <= 13) {
        return std::pair<uint16_t, uint16_t>(1, 1);
      } else if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(1, 2);
      } else if (level <= 39) {
        return std::pair<uint16_t, uint16_t>(1, 3);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(1, 4);
      } else if (level <= 53) {
        return std::pair<uint16_t, uint16_t>(1, 5);
      } else if (level <= 54) {
        return std::pair<uint16_t, uint16_t>(1, 6);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(1, 7);
      } else if (level <= 57) {
        return std::pair<uint16_t, uint16_t>(1, 9);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(1, 10);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(1, 12);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(1, 19);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(1, 30);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(1, 61);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(1, 63);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 2) {
      if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(2, 1);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(2, 2);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(2, 4);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(2, 5);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(2, 6);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(2, 10);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(2, 15);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(2, 31);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(2, 32);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 4) {
      if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(4, 1);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(4, 2);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(4, 3);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(4, 5);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(4, 8);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(4, 16);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 8) {
      if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(8, 1);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(8, 2);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(8, 3);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(8, 4);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(8, 8);
      } else {
        assert(0);
      }
    } else {
      assert(0);
    }
  }

  if (num_partitions == 16) {
    if (current_partition_size == 1) {
      if (level <= 13) {
        return std::pair<uint16_t, uint16_t>(1, 1);
      } else if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(1, 2);
      } else if (level <= 39) {
        return std::pair<uint16_t, uint16_t>(1, 3);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(1, 4);
      } else if (level <= 53) {
        return std::pair<uint16_t, uint16_t>(1, 5);
      } else if (level <= 54) {
        return std::pair<uint16_t, uint16_t>(1, 6);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(1, 7);
      } else if (level <= 57) {
        return std::pair<uint16_t, uint16_t>(1, 9);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(1, 10);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(1, 12);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(1, 19);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(1, 30);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(1, 61);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(1, 63);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 2) {
      if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(2, 1);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(2, 2);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(2, 4);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(2, 5);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(2, 6);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(2, 10);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(2, 15);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(2, 31);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(2, 32);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 4) {
      if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(4, 1);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(4, 2);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(4, 3);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(4, 5);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(4, 8);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(4, 16);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 8) {
      if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(8, 1);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(8, 2);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(8, 3);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(8, 4);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(8, 8);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 16) {
      if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(16, 1);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(16, 2);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(16, 4);
      } else {
        assert(0);
      }
    } else {
      assert(0);
    }
  }

  if (num_partitions == 12) {
    if (current_partition_size == 1) {
      if (level <= 13) {
        return std::pair<uint16_t, uint16_t>(1, 1);
      } else if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(1, 2);
      } else if (level <= 39) {
        return std::pair<uint16_t, uint16_t>(1, 3);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(1, 4);
      } else if (level <= 53) {
        return std::pair<uint16_t, uint16_t>(1, 5);
      } else if (level <= 54) {
        return std::pair<uint16_t, uint16_t>(1, 6);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(1, 7);
      } else if (level <= 57) {
        return std::pair<uint16_t, uint16_t>(1, 9);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(1, 10);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(1, 12);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(1, 19);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(1, 30);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(1, 61);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(1, 63);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 2) {
      if (level <= 26) {
        return std::pair<uint16_t, uint16_t>(2, 1);
      } else if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(2, 2);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(2, 4);
      } else if (level <= 58) {
        return std::pair<uint16_t, uint16_t>(2, 5);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(2, 6);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(2, 10);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(2, 15);
      } else if (level <= 62) {
        return std::pair<uint16_t, uint16_t>(2, 31);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(2, 32);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 4) {
      if (level <= 51) {
        return std::pair<uint16_t, uint16_t>(4, 1);
      } else if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(4, 2);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(4, 3);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(4, 5);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(4, 8);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(4, 16);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 8) {
      if (level <= 56) {
        return std::pair<uint16_t, uint16_t>(8, 1);
      } else if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(8, 2);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(8, 3);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(8, 4);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(8, 8);
      } else {
        assert(0);
      }
    } else if (current_partition_size == 12) {
      if (level <= 59) {
        return std::pair<uint16_t, uint16_t>(12, 1);
      } else if (level <= 60) {
        return std::pair<uint16_t, uint16_t>(12, 2);
      } else if (level <= 61) {
        return std::pair<uint16_t, uint16_t>(12, 3);
      } else if (level <= 63) {
        return std::pair<uint16_t, uint16_t>(12, 6);
      } else {
        assert(0);
      }
    } else {
      assert(0);
    }
  }

  assert(0);
  return std::pair(0, 0);
}

std::pair<uint16_t, uint16_t>
get_keyswitch_dnum_broadcast_28bit(uint16_t level, uint16_t num_partitions,
                                   uint16_t current_partition_size) {
  if (num_partitions == 1) {
    if (level <= 32) {
      return std::pair<uint16_t, uint16_t>(1, 1);
    } else if (level <= 42) {
      return std::pair<uint16_t, uint16_t>(1, 2);
    } else if (level <= 48) {
      return std::pair<uint16_t, uint16_t>(1, 3);
    } else if (level <= 51) {
      return std::pair<uint16_t, uint16_t>(1, 4);
    } else if (level <= 53) {
      return std::pair<uint16_t, uint16_t>(1, 5);
    } else if (level <= 54) {
      return std::pair<uint16_t, uint16_t>(1, 6);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 7);
    } else if (level <= 57) {
      return std::pair<uint16_t, uint16_t>(1, 9);
    } else if (level <= 58) {
      return std::pair<uint16_t, uint16_t>(1, 10);
    } else if (level <= 59) {
      return std::pair<uint16_t, uint16_t>(1, 12);
    } else if (level <= 60) {
      return std::pair<uint16_t, uint16_t>(1, 19);
    } else if (level <= 61) {
      return std::pair<uint16_t, uint16_t>(1, 30);
    } else if (level <= 62) {
      return std::pair<uint16_t, uint16_t>(1, 61);
    } else if (level <= 63) {
      return std::pair<uint16_t, uint16_t>(1, 63);
    } else {
      assert(0);
    }
  }

  if (num_partitions == 2) {
    if (level <= 32) {
      return std::pair<uint16_t, uint16_t>(1, 1);
    } else if (level <= 42) {
      return std::pair<uint16_t, uint16_t>(1, 2);
    } else if (level <= 48) {
      return std::pair<uint16_t, uint16_t>(1, 3);
    } else if (level <= 51) {
      return std::pair<uint16_t, uint16_t>(1, 4);
    } else if (level <= 53) {
      return std::pair<uint16_t, uint16_t>(1, 5);
    } else if (level <= 54) {
      return std::pair<uint16_t, uint16_t>(1, 6);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 7);
    } else if (level <= 57) {
      return std::pair<uint16_t, uint16_t>(1, 9);
    } else if (level <= 58) {
      return std::pair<uint16_t, uint16_t>(1, 10);
    } else if (level <= 59) {
      return std::pair<uint16_t, uint16_t>(1, 12);
    } else if (level <= 60) {
      return std::pair<uint16_t, uint16_t>(1, 19);
    } else if (level <= 61) {
      return std::pair<uint16_t, uint16_t>(1, 30);
    } else if (level <= 62) {
      return std::pair<uint16_t, uint16_t>(1, 61);
    } else if (level <= 63) {
      return std::pair<uint16_t, uint16_t>(1, 63);
    } else {
      assert(0);
    }
  }

  if (num_partitions == 4) {
    if (level <= 13) {
      return std::pair<uint16_t, uint16_t>(1, 1);
    } else if (level <= 26) {
      return std::pair<uint16_t, uint16_t>(1, 2);
    } else if (level <= 39) {
      return std::pair<uint16_t, uint16_t>(1, 3);
    } else if (level <= 51) {
      return std::pair<uint16_t, uint16_t>(1, 4);
    } else if (level <= 53) {
      return std::pair<uint16_t, uint16_t>(1, 5);
    } else if (level <= 54) {
      return std::pair<uint16_t, uint16_t>(1, 6);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 7);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 8);
    } else if (level <= 57) {
      return std::pair<uint16_t, uint16_t>(1, 9);
    } else if (level <= 58) {
      return std::pair<uint16_t, uint16_t>(1, 10);
    } else if (level <= 59) {
      return std::pair<uint16_t, uint16_t>(1, 12);
    } else if (level <= 60) {
      return std::pair<uint16_t, uint16_t>(1, 19);
    } else if (level <= 61) {
      return std::pair<uint16_t, uint16_t>(1, 30);
    } else if (level <= 62) {
      return std::pair<uint16_t, uint16_t>(1, 61);
    } else if (level <= 63) {
      return std::pair<uint16_t, uint16_t>(1, 63);
    } else {
      assert(0);
    }
  }

  if (num_partitions == 8) {
    if (level <= 13) {
      return std::pair<uint16_t, uint16_t>(1, 1);
    } else if (level <= 26) {
      return std::pair<uint16_t, uint16_t>(1, 2);
    } else if (level <= 39) {
      return std::pair<uint16_t, uint16_t>(1, 3);
    } else if (level <= 51) {
      return std::pair<uint16_t, uint16_t>(1, 4);
    } else if (level <= 53) {
      return std::pair<uint16_t, uint16_t>(1, 5);
    } else if (level <= 54) {
      return std::pair<uint16_t, uint16_t>(1, 6);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 7);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 8);
    } else if (level <= 57) {
      return std::pair<uint16_t, uint16_t>(1, 9);
    } else if (level <= 58) {
      return std::pair<uint16_t, uint16_t>(1, 10);
    } else if (level <= 59) {
      return std::pair<uint16_t, uint16_t>(1, 12);
    } else if (level <= 60) {
      return std::pair<uint16_t, uint16_t>(1, 19);
    } else if (level <= 61) {
      return std::pair<uint16_t, uint16_t>(1, 30);
    } else if (level <= 62) {
      return std::pair<uint16_t, uint16_t>(1, 61);
    } else if (level <= 63) {
      return std::pair<uint16_t, uint16_t>(1, 63);
    } else {
      assert(0);
    }
  }

  if (num_partitions == 12) {
    if (level <= 13) {
      return std::pair<uint16_t, uint16_t>(1, 1);
    } else if (level <= 26) {
      return std::pair<uint16_t, uint16_t>(1, 2);
    } else if (level <= 39) {
      return std::pair<uint16_t, uint16_t>(1, 3);
    } else if (level <= 51) {
      return std::pair<uint16_t, uint16_t>(1, 4);
    } else if (level <= 53) {
      return std::pair<uint16_t, uint16_t>(1, 5);
    } else if (level <= 54) {
      return std::pair<uint16_t, uint16_t>(1, 6);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 7);
    } else if (level <= 56) {
      return std::pair<uint16_t, uint16_t>(1, 8);
    } else if (level <= 57) {
      return std::pair<uint16_t, uint16_t>(1, 9);
    } else if (level <= 58) {
      return std::pair<uint16_t, uint16_t>(1, 10);
    } else if (level <= 59) {
      return std::pair<uint16_t, uint16_t>(1, 12);
    } else if (level <= 60) {
      return std::pair<uint16_t, uint16_t>(1, 19);
    } else if (level <= 61) {
      return std::pair<uint16_t, uint16_t>(1, 30);
    } else if (level <= 62) {
      return std::pair<uint16_t, uint16_t>(1, 61);
    } else if (level <= 63) {
      return std::pair<uint16_t, uint16_t>(1, 63);
    } else {
      assert(0);
    }
  }

  assert(0);
  return std::pair(0, 0);
}

} // namespace Backend
} // namespace Cinnamon