// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: AES-256 encryption and decryption implementation. Provides block cipher
// operations for secure data encryption with streaming support.

#pragma once

namespace crypto
{
	using byte_array = std::vector<uint8_t>;

	constexpr uint32_t BLOCK_SIZE = 16;

	class aes256
	{
	public:
		aes256(const byte_array& key);
		~aes256();

		static size_t encrypt(const byte_array& key, const byte_array& plain, byte_array& encrypted);
		static size_t encrypt(const byte_array& key, df::cspan plain, byte_array& encrypted);
		static size_t decrypt(const byte_array& key, const byte_array& encrypted, byte_array& plain);
		static size_t decrypt(const byte_array& key, df::cspan encrypted, byte_array& plain);

		// Zero from any encrypt entry point means the RNG failed and nothing was encrypted.
		size_t encrypt_start(size_t plain_length, byte_array& encrypted);
		size_t encrypt_continue(const byte_array& plain, byte_array& encrypted);
		size_t encrypt_continue(df::cspan plain, byte_array& encrypted);
		size_t encrypt_end(byte_array& encrypted);

		size_t decrypt_start(size_t encrypted_length);
		size_t decrypt_continue(const byte_array& encrypted, byte_array& plain);
		size_t decrypt_continue(df::cspan encrypted, byte_array& plain);
		static size_t decrypt_end(const byte_array& plain);

	private:
		byte_array m_key;
		byte_array m_salt;
		byte_array m_rkey;

		uint8_t m_buffer[3 * BLOCK_SIZE];
		uint8_t m_buffer_pos;
		size_t m_remainingLength;

		bool m_decryptInitialized;
		uint8_t m_iv[BLOCK_SIZE];
		bool m_ivRead;

		void check_and_encrypt_buffer(byte_array& encrypted);
		void check_and_decrypt_buffer(byte_array& plain);

		void encrypt(uint8_t* buffer);
		void decrypt(uint8_t* buffer);

		void expand_enc_key(uint8_t* rc);
		void expand_dec_key(uint8_t* rc);

		static void sub_bytes(uint8_t* buffer);
		static void sub_bytes_inv(uint8_t* buffer);

		void copy_key();

		void add_round_key(uint8_t* buffer, uint8_t round) const;

		static void shift_rows(uint8_t* buffer);
		static void shift_rows_inv(uint8_t* buffer);

		static void mix_columns(uint8_t* buffer);
		static void mix_columns_inv(uint8_t* buffer);
	};
}
