// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: File I/O helpers. Provides memory-mapped file reading, stream
// utilities, and buffered I/O operations.

#pragma once

namespace df
{
	static bool is_int32(const int64_t n)
	{
		return n >= 0 && n <= INT32_MAX;
	}

	class file final : public no_copy
	{
		platform::file_ptr _h;
		mutable std::unique_ptr<uint8_t, free_delete> _buffer;
		mutable size_t _buffer_data_size = 0;

	public:
		file() = default;

		file(platform::file_ptr h) : _h(std::move(h))
		{
			_h->seek(0, platform::file::whence::begin);
		}

		~file() override
		{
			_h.reset();
			_buffer.reset();
		}

		bool open_read(const file_path path, const bool sequential_scan = false)
		{
			_h = open_file(path, sequential_scan
				                     ? platform::file_open_mode::sequential_scan
				                     : platform::file_open_mode::read);
			return _h != nullptr;
		}

		bool write(const void* data, const uint64_t write64) const
		{
			if (!is_int32(write64) || !_h)
			{
				assert_true(false);
				return false;
			}

			const size_t write = static_cast<uint32_t>(write64);
			return _h->write(static_cast<const uint8_t*>(data), write) == write;
		}

		bool read(void* result, const int64_t wanted64) const
		{
			if (!is_int32(wanted64) || !_h)
			{
				assert_true(false);
				return false;
			}

			const auto wanted = static_cast<uint32_t>(wanted64);
			return _h->read(static_cast<uint8_t*>(result), wanted) == wanted;
		}

		std::vector<uint8_t> read(const uint64_t wanted64) const
		{
			std::vector<uint8_t> result;

			if (is_int32(wanted64) && wanted64 < one_meg)
			{
				const auto wanted = static_cast<uint32_t>(wanted64);
				result.resize(wanted);
				const auto read = _h->read(result.data(), wanted);
				result.resize(static_cast<size_t>(read), 0);
			}

			return result;
		}

		bool read64k() const
		{
			if (!_buffer)
			{
				_buffer = df::unique_alloc<uint8_t>(sixty_four_k);
			}

			constexpr auto wanted = static_cast<uint32_t>(sixty_four_k);
			const auto read = _h->read(_buffer.get(), wanted);
			_buffer_data_size = static_cast<size_t>(read);
			return read > 0;
		}

		const uint8_t* buffer() const
		{
			return _buffer.get();
		}

		size_t buffer_data_size() const
		{
			return _buffer_data_size;
		}

		blob read_blob(const size_t size) const
		{
			blob result(size);
			// Deliberately not a ternary: that copy-initializes result into a prvalue instead of moving it.
			if (_h->read(result.data(), size) != size) return {};
			return result;
		}

		bool insert(const uint8_t* data, const int64_t dataSize, const int64_t start = 0,
		            const int64_t replace = 0) const
		{
			const auto size = file_size();
			const auto delta = dataSize - replace;

			constexpr uint64_t buffer_size = sixty_four_k;
			const auto buffer = df::unique_alloc<char>(buffer_size);

			if (delta < 0)
			{
				// Move up
				auto pos = start;
				auto remaining = size - (start + replace);

				while (remaining > 0)
				{
					const auto blockSize = std::min(remaining, buffer_size);

					if (!seek_from_begin(pos) || !read(buffer.get(), blockSize) ||
						!seek_from_begin(pos + delta) ||
						!write(buffer.get(), blockSize))
					{
						assert_true(false);
						return false;
					}

					pos += blockSize;
					remaining -= blockSize;
				}

				_h->trunc(size + delta);
			}
			else if (delta > 0)
			{
				// move down
				auto pos = size;
				auto remaining = size - (start + replace);

				while (remaining > 0)
				{
					const auto block_size = std::min(remaining, buffer_size);

					if (!seek_from_begin(pos - block_size) ||
						!read(buffer.get(), block_size) ||
						!seek_from_begin(pos + delta - block_size) ||
						!write(buffer.get(), block_size))
					{
						assert_true(false);
						return false;
					}

					pos -= block_size;
					remaining -= block_size;
				}
			}

			return seek_from_begin(start) && write(data, dataSize);
		}

		bool seek_from_begin(const int64_t offset) const
		{
			return _h->seek(offset, platform::file::whence::begin) == offset;
		}

		uint64_t file_size() const
		{
			return _h->size();
		}
	};
}
