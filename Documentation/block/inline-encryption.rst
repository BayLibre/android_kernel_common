.. SPDX-License-Identifier: GPL-2.0

=================
Inline Encryption
=================

Background
==========

Inline encryption hardware sits logically between memory and the disk, and can
en/decrypt data as it goes in/out of the disk. Inline encryption hardware has a
fixed number of "keyslots" - slots into which encryption contexts (i.e. the
encryption key, encryption algorithm, data unit size) can be programmed by the
kernel at any time. Each request sent to the disk can be tagged with the index
of a keyslot (and also a data unit number to act as an encryption tweak), and
the inline encryption hardware will en/decrypt the data in the request with the
encryption context programmed into that keyslot. This is very different from
full disk encryption solutions like self encrypting drives/TCG OPAL/ATA
Security standards, since with inline encryption, any block on disk could be
encrypted with any encryption context the kernel chooses.


Objective
=========

We want to support inline encryption (IE) in the kernel.
To allow for testing, we also want a crypto API fallback when actual
IE hardware is absent. We also want IE to work with layered devices
like dm and loopback (i.e. we want to be able to use the IE hardware
of the underlying devices if present, or else fall back to crypto API
en/decryption).


Constraints and notes
=====================

- IE hardware has a limited number of "keyslots" that can be programmed
  with an encryption context (key, algorithm, data unit size, etc.) at any time.
  One can specify a keyslot in a data request made to the device, and the
  device will en/decrypt the data using the encryption context programmed into
  that specified keyslot. When possible, we want to make multiple requests with
  the same encryption context share the same keyslot.

- We need a way for upper layers like filesystems to specify an encryption
  context to use for en/decrypting a struct bio, and a device driver (like UFS)
  needs to be able to use that encryption context when it processes the bio.

- We need a way for device drivers to expose their capabilities in a unified
  way to the upper layers.


Design
======

We add a :c:type:`struct bio_crypt_ctx` to :c:type:`struct bio` that can
represent an encryption context, because we need to be able to pass this
encryption context from the FS layer to the device driver to act upon.

While IE hardware works on the notion of keyslots, the FS layer has no
knowledge of keyslots - it simply wants to specify an encryption context to
use while en/decrypting a bio.

We introduce a keyslot manager (KSM) that handles the translation from
encryption contexts specified by the FS to keyslots on the IE hardware.
This KSM also serves as the way IE hardware can expose its capabilities to
upper layers. The generic mode of operation is: each device driver that wants
to support IE will construct a KSM and set it up in its struct request_queue.
Upper layers that want to use IE on this device can then use this KSM in
the device's struct request_queue to translate an encryption context into
a keyslot. The presence of the KSM in the request queue shall be used to mean
that the device supports IE.

On the device driver end of the interface, the device driver needs to tell the
KSM how to actually manipulate the IE hardware in the device to do things like
programming the crypto key into the IE hardware into a particular keyslot. All
this is achieved through the :c:type:`struct keyslot_mgmt_ll_ops` that the
device driver passes to the KSM when creating it.

It uses refcounts to track which keyslots are idle (either they have no
encryption context programmed, or there are no in-flight struct bios
referencing that keyslot). When a new encryption context needs a keyslot, it
tries to find a keyslot that has already been programmed with the same
encryption context, and if there is no such keyslot, it evicts the least
recently used idle keyslot and programs the new encryption context into that
one. If no idle keyslots are available, then the caller will sleep until there
is at least one.


blk-mq changes, other block layer changes and blk-crypto-fallback
=================================================================

We add a pointer to a ``bi_crypt_context`` and ``keyslot`` to
:c:type:`struct request`. These will be referred to as the ``crypto fields``
for the request. This ``keyslot`` is the keyslot into which the
``bi_crypt_context`` has been programmed into in the keyslot manager of the
``request_queue`` that this request is being sent to.

We introduce ``block/blk-crypto-fallback.c``, which allows upper layers to remain
blissfully unaware of whether or not real inline encryption hardware is present
underneath.

When a bio is submitted with a target ``request_queue`` that doesn't support the
encryption context specified with the bio, the block layer will en/decrypt the
bio with the blk-crypto-fallback.

If the bio is a ``WRITE`` bio, a bounce bio is allocated, and the data in the bio
is encrypted stored in the bounce bio - blk-mq will then proceed to process the
bounce bio as if it were not encrypted at all (except when blk-integrity is
concerned). ``blk-crypto-fallback`` sets the bounce bio's ``bi_end_io`` to an
internal function that cleans up the bounce bio and ends the original bio.

If the bio is a ``READ`` bio, the bio's ``bi_end_io`` (and also ``bi_private``)
is saved and overwritten by ``blk-crypto-fallback`` to
``bio_crypto_fallback_decrypt_bio``.  The bio's ``bi_crypt_context`` is also
overwritten with ``NULL``, so that to the rest of the stack, the bio looks
as if it was a regular bio that never had an encryption context specified.
``bio_crypto_fallback_decrypt_bio`` will decrypt the bio, restore the original
``bi_end_io`` (and also ``bi_private``) and end the bio again.

If we reach a point when a :c:type:`struct request` needs to be allocated for a
bio that still has an encryption context, that means that the bio was not
handled by the ``blk-crypto-fallback``, which means that the underlying inline
encryption hardware claimed to support the encryption context specified with the
bio. So in this situation, blk-mq tries to program the encryption context into
the ``request_queue``'s keyslot_manager, and obtain a keyslot, which it stores
in its newly added ``keyslot`` field. This keyslot is released when the request
is completed.

When a bio is added to a request, the request takes over ownership of the
``bi_crypt_context`` of the bio - in particular, the request keeps the
``bi_crypt_context`` of the first bio in its bio-list, and frees the rest
(blk-mq needs to be careful to maintain this invariant during bio and request
merges).

To make it possible for inline encryption to work with request queue based
layered devices, when a request is cloned, its ``crypto fields`` are cloned as
well. When the cloned request is submitted, blk-mq programs the
``bi_crypt_context`` of the request into the clone's request_queue's keyslot
manager, and stores the returned keyslot in the clone's ``keyslot``.


Layered Devices
===============

Request queue based layered devices like dm-rq that wish to support IE need to
create their own keyslot manager for their request queue, and expose whatever
functionality they choose. When a layered device wants to pass a clone of that
request to another ``request_queue``, blk-crypto will initialize and prepare the
clone as necessary - see ``blk_crypto_rq_prep_clone`` and
``blk_crypto_insert_cloned_request`` in ``blk-crypto.c``.


Future Optimizations for layered devices
========================================

Creating a keyslot manager for a layered device uses up memory for each
keyslot, and in general, a layered device merely passes the request on to a
"child" device, so the keyslots in the layered device itself are completely
unused, and don't need any refcounting or keyslot programming. We can instead
define a new type of KSM; the "passthrough KSM", that layered devices can use
to advertise an unlimited number of keyslots, and support for any encryption
algorithms they choose, while not actually using any memory for each keyslot.
Another use case for the "passthrough KSM" is for IE devices that do not have a
limited number of keyslots.


Interaction between inline encryption and blk integrity
=======================================================

At the time of this patch, there is no real hardware that supports both these
features. However, these features do interact with each other, and it's not
completely trivial to make them both work together properly. In particular,
when a WRITE bio wants to use inline encryption on a device that supports both
features, the bio will have an encryption context specified, after which
its integrity information is calculated (using the plaintext data, since
the encryption will happen while data is being written), and the data and
integrity info is sent to the device. Obviously, the integrity info must be
verified before the data is encrypted. After the data is encrypted, the device
must not store the integrity info that it received with the plaintext data
since that might reveal information about the plaintext data. As such, it must
re-generate the integrity info from the ciphertext data and store that on disk
instead. Another issue with storing the integrity info of the plaintext data is
that it changes the on disk format depending on whether hardware inline
encryption support is present or the kernel crypto API fallback is used (since
if the fallback is used, the device will receive the integrity info of the
ciphertext, not that of the plaintext).

Because there isn't any real hardware yet, it seems prudent to assume that
hardware implementations might not implement both features together correctly,
and disallow the combination for now. Whenever a device supports integrity, the
kernel will pretend that the device does not support hardware inline encryption
(by essentially setting the keyslot manager in the request_queue of the device
to NULL). When the crypto API fallback is enabled, this means that all bios with
and encryption context will use the fallback, and IO will complete as usual.
When the fallback is disabled, a bio with an encryption context will be failed.
