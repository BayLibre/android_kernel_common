// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Utilities for working with ranges of indices.

/// A range of indices.
///
/// This utility is useful for ensuring that no index in the range is used more than once.
#[derive(Debug)]
pub struct Range {
    offset: usize,
    length: usize,
}

impl Range {
    /// Creates a new `Range` for an area of the given size.
    pub fn new_area(size: usize) -> Self {
        Self {
            offset: 0,
            length: size,
        }
    }

    /// Creates a new `Range` with the given offset and size.
    ///
    /// Be careful when using this method, as it allows you to use a range twice.
    pub fn new_careful(offset: usize, length: usize) -> Self {
        Self { offset, length }
    }

    /// Use this range of indices.
    ///
    /// This destroys the `Range` object, so these indices cannot be used again after this call.
    pub fn use_range(self) -> UsedRange {
        UsedRange {
            offset: self.offset,
            length: self.length,
        }
    }

    /// Duplicate this range.
    ///
    /// Be careful when using this method, as it allows you to use a range twice.
    pub fn duplicate_range_careful(&self) -> Range {
        Range {
            offset: self.offset,
            length: self.length,
        }
    }

    /// Peek at the offset without using the range.
    ///
    /// This doesn't destroy the `Range` object, so be careful that the range is not used twice.
    pub fn peek_offset(&self) -> usize {
        self.offset
    }

    /// Peek at the length without using the range.
    ///
    /// This doesn't destroy the `Range` object, so be careful that the range is not used twice.
    pub fn peek_length(&self) -> usize {
        self.length
    }

    /// Peek at the end without using the range.
    ///
    /// This doesn't destroy the `Range` object, so be careful that the range is not used twice.
    pub fn peek_end(&self) -> Result<usize, RangeError> {
        self.offset.checked_add(self.length).ok_or(RangeError)
    }

    /// Truncates this range to the given length.
    pub fn truncate(&mut self, length: usize) -> Result<(), RangeError> {
        if length > self.length {
            return Err(RangeError);
        }
        self.length = length;
        Ok(())
    }

    /// Assert that this range is aligned properly.
    pub fn assert_aligned(&self, alignment: usize) -> Result<(), RangeError> {
        if self.offset % alignment == 0 {
            Ok(())
        } else {
            Err(RangeError)
        }
    }

    /// Assert that this range has the expected length.
    pub fn assert_length_eq(&self, length: usize) -> Result<(), RangeError> {
        if self.length == length {
            Ok(())
        } else {
            Err(RangeError)
        }
    }

    /// Assert that this range is empty.
    pub fn assert_empty(self) -> Result<(), RangeError> {
        self.assert_length_eq(0)
    }

    /// Splits the range into two sub-ranges.
    ///
    /// Fails if the `length` is greater than the range being split.
    pub fn split_within(mut self, length: usize) -> Result<(Range, Range), RangeError> {
        let left = self.take_from_start(length)?;
        Ok((left, self))
    }

    /// Splits the range into two sub-ranges.
    ///
    /// Fails if the `position` is not within the current range.
    pub fn split_at(mut self, position: usize) -> Result<(Range, Range), RangeError> {
        let left = self.take_until(position)?;
        Ok((left, self))
    }

    /// Modify this range by taking the first `length` bytes.
    pub fn take_until(&mut self, position: usize) -> Result<Range, RangeError> {
        let from_start = Range {
            offset: self.offset,
            length: position.checked_sub(self.offset).ok_or(RangeError)?,
        };

        let new_self = Range {
            offset: position,
            length: self
                .length
                .checked_sub(from_start.length)
                .ok_or(RangeError)?,
        };

        *self = new_self;

        Ok(from_start)
    }

    /// Modify this range by taking the first `length` bytes.
    pub fn take_from_start(&mut self, length: usize) -> Result<Range, RangeError> {
        let from_start = Range {
            offset: self.offset,
            length: length,
        };

        let new_self = Range {
            offset: self.offset.checked_add(length).ok_or(RangeError)?,
            length: self.length.checked_sub(length).ok_or(RangeError)?,
        };

        *self = new_self;

        Ok(from_start)
    }

    /// Split this range into sub-ranges of the given size.
    pub fn iter_chunks(self, chunk_size: usize) -> Result<ChunkIter, RangeError> {
        if self.length % chunk_size != 0 {
            return Err(RangeError);
        }

        Ok(ChunkIter {
            pos: self.offset,
            end: self.offset.checked_add(self.length).ok_or(RangeError)?,
            chunk_size,
        })
    }
}

/// An iterator over ranges of the same size.
pub struct ChunkIter {
    pos: usize,
    end: usize,
    chunk_size: usize,
}

impl Iterator for ChunkIter {
    type Item = Range;
    fn next(&mut self) -> Option<Range> {
        if self.pos >= self.end {
            return None;
        }

        let range = Range {
            offset: self.pos,
            length: self.chunk_size,
        };
        self.pos = self.pos + self.chunk_size;

        Some(range)
    }
}

/// A version of [`Range`] where the length is a compile-time constant.
///
/// This can be used to store a `Range` without using up space to store the length.
pub struct RangeFixedSize<const LENGTH: usize> {
    offset: usize,
}

impl<const LENGTH: usize> RangeFixedSize<LENGTH> {
    /// Create a `RangeFixedSize` from a `Range`.
    pub fn from_range(range: Range) -> Result<Self, RangeError> {
        if range.length == LENGTH {
            Ok(Self {
                offset: range.offset,
            })
        } else {
            Err(RangeError)
        }
    }

    /// Convert this back into a `Range`.
    pub fn into_range(self) -> Range {
        Range {
            offset: self.offset,
            length: LENGTH,
        }
    }
}

/// The return value of [`Range::use_range`].
///
/// The only way to access the indices in a range is to mark it "used", which converts it into this
/// type, destroying the original [`Range`] object.
#[derive(Copy, Clone)]
pub struct UsedRange {
    /// The offset of this range.
    pub offset: usize,
    /// The length of this range.
    pub length: usize,
}

/// The error type returned when ranges are used incorrectly.
pub struct RangeError;

impl From<RangeError> for crate::error::Error {
    fn from(_range: RangeError) -> crate::error::Error {
        crate::error::code::EINVAL
    }
}
