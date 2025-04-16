// SPDX-License-Identifier: GPL-2.0

use super::*;

pub(crate) fn make_numbers() -> Result<KVec<i32>> {
    let mut numbers = KVec::new();
    numbers.push(72, GFP_KERNEL)?;
    numbers.push(108, GFP_KERNEL)?;
    numbers.push(200, GFP_KERNEL)?;
    Ok(numbers)
}
