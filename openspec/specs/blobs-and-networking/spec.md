# blobs-and-networking Specification

## Purpose

`Blob`, `FileReader` and the parts of XHR that carry bytes rather than text.

React Native's JavaScript for all of this is shared and expects a `BlobModule`
underneath it. Without one, `new Blob()` throws at import and takes anything that
touched it down with it -- which is most of what fetches a file.

## Requirements

### Requirement: A blob is stored by handle and read back whole

The system SHALL keep blob contents natively, addressed by the handle React
Native's JavaScript holds, and SHALL return exactly the bytes that were stored.

An unknown handle SHALL be reported as absent rather than as an empty blob: a
released handle read back is a bug in the app, and an empty string hides it.

Binary content SHALL survive unchanged, including bytes that are not valid text.

#### Scenario: A stored blob reads back

- **WHEN** a blob is created and then read
- **THEN** the bytes returned are the bytes stored

#### Scenario: An unknown handle is absent

- **WHEN** a blob that was never stored, or was released, is read
- **THEN** the read reports it as absent rather than returning empty content

#### Scenario: Releasing frees it

- **WHEN** JavaScript releases a blob
- **THEN** it is no longer readable

### Requirement: A blob can be sliced

The system SHALL implement `Blob.slice`, returning the requested range.

A range that runs past the end SHALL be clamped to what exists rather than
rejected, which is what the web platform does and what React Native's JavaScript
expects.

#### Scenario: A slice takes the part asked for

- **WHEN** a blob is sliced between two offsets
- **THEN** the result is exactly that range

#### Scenario: A slice past the end is clamped

- **WHEN** a slice asks for more than the blob holds
- **THEN** the result is the remainder, and the call does not fail

### Requirement: Base64 round-trips what a server actually sends

The system SHALL encode and decode base64 for `FileReader.readAsDataURL` and for
request and response bodies, handling every tail length, emitting the usual
padding, and decoding content that is not text.

Whitespace in encoded input SHALL be ignored, and a truncated final group SHALL
be tolerated rather than throwing, because both appear in real responses.

#### Scenario: Every tail length round-trips

- **WHEN** content of any length is encoded and decoded again
- **THEN** the result is identical to the original

#### Scenario: Real-world encodings decode

- **WHEN** encoded content contains whitespace or a truncated final group
- **THEN** it decodes rather than failing

### Requirement: What is not implemented says so

The system SHALL fail by name for the parts of this surface it does not
implement -- `responseType: 'blob'`, blob request bodies, binary WebSocket
frames, and `readAsText` of an encoding other than UTF-8 -- rather than
returning empty or wrong content.

#### Scenario: An unsupported read is refused clearly

- **WHEN** an app asks for a decoding this platform does not implement
- **THEN** the call rejects naming what is missing, rather than resolving with
  something plausible and wrong
