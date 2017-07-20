#pragma once

////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2017 Nicholas Frechette & Animation Compression Library contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#include "acl/core/memory.h"
#include "acl/core/error.h"
#include "acl/math/quat_32.h"
#include "acl/math/quat_packing.h"
#include "acl/math/vector4_32.h"
#include "acl/math/vector4_packing.h"
#include "acl/compression/stream/track_stream.h"
#include "acl/compression/stream/sample_streams.h"
#include "acl/compression/skeleton_error_metric.h"

#include <stdint.h>

namespace acl
{
	namespace impl
	{
		inline void quantize_fixed_rotation_stream(Allocator& allocator, const RotationTrackStream& raw_stream, RotationFormat8 rotation_format, RotationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(Vector4_32)
			ACL_ENSURE(raw_stream.get_sample_size() == sizeof(Vector4_32), "Unexpected rotation sample size. %u != %u", raw_stream.get_sample_size(), sizeof(Vector4_32));

			uint32_t num_samples = raw_stream.get_num_samples();
			uint32_t rotation_sample_size = get_packed_rotation_size(rotation_format);
			uint32_t sample_rate = raw_stream.get_sample_rate();
			RotationTrackStream quantized_stream(allocator, num_samples, rotation_sample_size, sample_rate, rotation_format);

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				Quat_32 rotation = raw_stream.get_raw_sample<Quat_32>(sample_index);
				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

				switch (rotation_format)
				{
				case RotationFormat8::Quat_128:
					pack_vector4_128(quat_to_vector(rotation), quantized_ptr);
					break;
				case RotationFormat8::QuatDropW_96:
					pack_vector3_96(quat_to_vector(rotation), quantized_ptr);
					break;
				case RotationFormat8::QuatDropW_48:
					pack_vector3_48(quat_to_vector(rotation), quantized_ptr);
					break;
				case RotationFormat8::QuatDropW_32:
					pack_vector3_32<11, 11, 10>(quat_to_vector(rotation), quantized_ptr);
					break;
				case RotationFormat8::QuatDropW_Variable:
				default:
					ACL_ENSURE(false, "Invalid or unsupported rotation format: %s", get_rotation_format_name(rotation_format));
					break;
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_fixed_rotation_streams(Allocator& allocator, BoneStreams* bone_streams, uint16_t num_bones, RotationFormat8 rotation_format, bool is_variable_variant)
		{
			const RotationVariant8 rotation_variant = get_rotation_variant(rotation_format);
			const RotationFormat8 highest_bit_rate = get_highest_variant_precision(rotation_variant);

			// By the time we get here, values have been converted to their final format, and normalized if selected
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				BoneStreams& bone_stream = bone_streams[bone_index];

				// Default tracks aren't quantized
				if (bone_stream.is_rotation_default)
					continue;

				// If our format isn't variable, we allow constant tracks to be quantized to any format
				// If our format is variable, we keep them fixed at the highest bit rate in the variant
				RotationFormat8 format = is_variable_variant && bone_stream.is_rotation_constant ? highest_bit_rate : rotation_format;

				quantize_fixed_rotation_stream(allocator, bone_stream.rotations, format, bone_stream.rotations);
			}
		}

		inline void quantize_fixed_rotation_stream(Allocator& allocator, const RotationTrackStream& raw_stream, uint8_t bit_rate, RotationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(Vector4_32)
			ACL_ENSURE(raw_stream.get_sample_size() == sizeof(Vector4_32), "Unexpected rotation sample size. %u != %u", raw_stream.get_sample_size(), sizeof(Vector4_32));

			uint32_t num_samples = raw_stream.get_num_samples();
			uint32_t sample_size = sizeof(uint64_t);
			uint32_t sample_rate = raw_stream.get_sample_rate();
			RotationTrackStream quantized_stream(allocator, num_samples, sample_size, sample_rate, RotationFormat8::QuatDropW_Variable, bit_rate);

			uint8_t num_bits_at_bit_rate = get_num_bits_at_bit_rate(bit_rate);

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				Quat_32 rotation = raw_stream.get_raw_sample<Quat_32>(sample_index);
				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

				pack_vector3_n(quat_to_vector(rotation), num_bits_at_bit_rate, num_bits_at_bit_rate, num_bits_at_bit_rate, quantized_ptr);
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_fixed_rotation_streams(Allocator& allocator, BoneStreams* bone_streams, uint16_t num_bones, uint8_t bit_rate)
		{
			const RotationFormat8 highest_bit_rate = get_highest_variant_precision(RotationVariant8::QuatDropW);

			// By the time we get here, values have been converted to their final format, and normalized if selected
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				BoneStreams& bone_stream = bone_streams[bone_index];

				// Default tracks aren't quantized
				if (bone_stream.is_rotation_default)
					continue;

				// If our format is variable, we keep them fixed at the highest bit rate in the variant
				if (bone_stream.is_rotation_constant)
					quantize_fixed_rotation_stream(allocator, bone_stream.rotations, highest_bit_rate, bone_stream.rotations);
				else
					quantize_fixed_rotation_stream(allocator, bone_stream.rotations, bit_rate, bone_stream.rotations);
			}
		}

		inline void quantize_fixed_translation_stream(Allocator& allocator, const TranslationTrackStream& raw_stream, VectorFormat8 translation_format, TranslationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(Vector4_32)
			ACL_ENSURE(raw_stream.get_sample_size() == sizeof(Vector4_32), "Unexpected translation sample size. %u != %u", raw_stream.get_sample_size(), sizeof(Vector4_32));
			ACL_ENSURE(raw_stream.get_vector_format() == VectorFormat8::Vector3_96, "Expected a Vector3_96 vector format, found: %s", get_vector_format_name(raw_stream.get_vector_format()));

			uint32_t num_samples = raw_stream.get_num_samples();
			uint32_t sample_size = get_packed_vector_size(translation_format);
			uint32_t sample_rate = raw_stream.get_sample_rate();
			TranslationTrackStream quantized_stream(allocator, num_samples, sample_size, sample_rate, translation_format);

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				Vector4_32 translation = raw_stream.get_raw_sample<Vector4_32>(sample_index);
				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

				switch (translation_format)
				{
				case VectorFormat8::Vector3_96:
					pack_vector3_96(translation, quantized_ptr);
					break;
				case VectorFormat8::Vector3_48:
					pack_vector3_48(translation, quantized_ptr);
					break;
				case VectorFormat8::Vector3_32:
					pack_vector3_32<11, 11, 10>(translation, quantized_ptr);
					break;
				case VectorFormat8::Vector3_Variable:
				default:
					ACL_ENSURE(false, "Invalid or unsupported vector format: %s", get_vector_format_name(translation_format));
					break;
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_fixed_translation_streams(Allocator& allocator, BoneStreams* bone_streams, uint16_t num_bones, VectorFormat8 translation_format)
		{
			// By the time we get here, values have been converted to their final format, and normalized if selected
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				BoneStreams& bone_stream = bone_streams[bone_index];

				// Default tracks aren't quantized
				if (bone_stream.is_translation_default)
					continue;

				// Constant translation tracks store the remaining sample with full precision
				VectorFormat8 format = bone_stream.is_translation_constant ? VectorFormat8::Vector3_96 : translation_format;

				quantize_fixed_translation_stream(allocator, bone_stream.translations, format, bone_stream.translations);
			}
		}

		inline void quantize_fixed_translation_stream(Allocator& allocator, const TranslationTrackStream& raw_stream, uint8_t bit_rate, TranslationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(Vector4_32)
			ACL_ENSURE(raw_stream.get_sample_size() == sizeof(Vector4_32), "Unexpected translation sample size. %u != %u", raw_stream.get_sample_size(), sizeof(Vector4_32));
			ACL_ENSURE(raw_stream.get_vector_format() == VectorFormat8::Vector3_96, "Expected a Vector3_96 vector format, found: %s", get_vector_format_name(raw_stream.get_vector_format()));

			uint32_t num_samples = raw_stream.get_num_samples();
			uint32_t sample_size = sizeof(uint64_t);
			uint32_t sample_rate = raw_stream.get_sample_rate();
			TranslationTrackStream quantized_stream(allocator, num_samples, sample_size, sample_rate, VectorFormat8::Vector3_Variable, bit_rate);

			uint8_t num_bits_at_bit_rate = get_num_bits_at_bit_rate(bit_rate);

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				Vector4_32 translation = raw_stream.get_raw_sample<Vector4_32>(sample_index);
				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

				pack_vector3_n(translation, num_bits_at_bit_rate, num_bits_at_bit_rate, num_bits_at_bit_rate, quantized_ptr);
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_fixed_translation_streams(Allocator& allocator, BoneStreams* bone_streams, uint16_t num_bones, uint8_t bit_rate)
		{
			// By the time we get here, values have been converted to their final format, and normalized if selected
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				BoneStreams& bone_stream = bone_streams[bone_index];

				// Default tracks aren't quantized
				if (bone_stream.is_translation_default)
					continue;

				// Constant translation tracks store the remaining sample with full precision
				if (bone_stream.is_translation_constant)
					quantize_fixed_translation_stream(allocator, bone_stream.translations, VectorFormat8::Vector3_96, bone_stream.translations);
				else
					quantize_fixed_translation_stream(allocator, bone_stream.translations, bit_rate, bone_stream.translations);
			}
		}

		inline void quantize_variable_streams(Allocator& allocator, BoneStreams* bone_streams, uint16_t num_bones, RotationFormat8 rotation_format, VectorFormat8 translation_format, const AnimationClip& clip, const RigidSkeleton& skeleton)
		{
			// Duplicate our streams
			BoneStreams* quantized_streams = allocate_type_array<BoneStreams>(allocator, num_bones);
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
				quantized_streams[bone_index] = bone_streams[bone_index].duplicate();

			const bool is_rotation_variable = is_rotation_format_variable(rotation_format);
			const bool is_translation_variable = is_vector_format_variable(translation_format);
			const bool scan_whole_clip_for_bad_bone = false;

			// Quantize everything to the lowest bit rate of the same variant
			if (is_rotation_variable)
				quantize_fixed_rotation_streams(allocator, quantized_streams, num_bones, LOWEST_BIT_RATE);
			else
				quantize_fixed_rotation_streams(allocator, quantized_streams, num_bones, rotation_format, false);

			if (is_translation_variable)
				quantize_fixed_translation_streams(allocator, quantized_streams, num_bones, LOWEST_BIT_RATE);
			else
				quantize_fixed_translation_streams(allocator, quantized_streams, num_bones, translation_format);

			uint32_t num_samples = get_animated_num_samples(bone_streams, num_bones);
			float sample_rate = float(bone_streams[0].rotations.get_sample_rate());
			float error_threshold = clip.get_error_threshold();
			float clip_duration = clip.get_duration();
			float error = std::numeric_limits<float>::max();

			// TODO: Use the original un-quantized bone streams?
			// It seems to yield a smaller memory footprint but it could be dangerous since our data diverges from
			// the 64bit original clip and we might also be normalized, adding further loss
			// Basically by using the bone streams, the error we measure is compared to the possibly normalized,
			// converted rotations, etc.
			constexpr bool use_clip_as_ref = true;

			Transform_32* raw_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);
			Transform_32* lossy_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);
			float* error_per_bone = allocate_type_array<float>(allocator, num_bones);
			BoneTrackError* error_per_stream = allocate_type_array<BoneTrackError>(allocator, num_bones);

			uint32_t bitset_size = get_bitset_size(num_bones);
			uint32_t* low_resolution_bones = allocate_type_array<uint32_t>(allocator, bitset_size);

			bitset_reset(low_resolution_bones, bitset_size, false);

			// While we are above our precision threshold, iterate
			while (error > error_threshold)
			{
				error = 0.0f;

				// Scan the whole clip, and find the bone with the worst error across the whole clip
				uint16_t bad_bone_index = INVALID_BONE_INDEX;
				float worst_clip_error = error_threshold;
				for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
				{
					// Sample our streams and calculate the error
					float sample_time = min(float(sample_index) / sample_rate, clip_duration);

					if (use_clip_as_ref)
						clip.sample_pose(sample_time, raw_local_pose, num_bones);
					else
						sample_streams(bone_streams, num_bones, sample_time, raw_local_pose);

					sample_streams(quantized_streams, num_bones, sample_time, lossy_local_pose);

					calculate_skeleton_error(allocator, skeleton, raw_local_pose, lossy_local_pose, error_per_bone);

					// Find first bone in the hierarchy that is above our threshold (root first)
					for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
					{
						if (error_per_bone[bone_index] > worst_clip_error && !bitset_test(low_resolution_bones, bitset_size, bone_index))
						{
							worst_clip_error = error_per_bone[bone_index];
							error = error_per_bone[bone_index];
							bad_bone_index = bone_index;
							break;
						}
					}

					if (!scan_whole_clip_for_bad_bone && bad_bone_index != INVALID_BONE_INDEX)
						break;
				}

				if (bad_bone_index == INVALID_BONE_INDEX)
				{
					// We probably have some low resolution bones for some reason, stop nows
					break;
				}

				// Find which bone in the chain contributes the most error that isn't at the highest precision
				calculate_skeleton_error_contribution(skeleton, raw_local_pose, lossy_local_pose, bad_bone_index, error_per_stream);

				uint16_t target_bone_index = INVALID_BONE_INDEX;
				AnimationTrackType8 target_track_type = AnimationTrackType8::Rotation;
				float worst_track_error = 0.0f;

				// We search starting at the root bone, by increasing the precision of a bone higher up, we retain more children
				// with lower precision, and keep the memory footprint lower as a result
				uint16_t current_bone_index = bad_bone_index;
				while (current_bone_index != INVALID_BONE_INDEX)
				{
					// Only select the stream if we can still increase its precision
					uint8_t rotation_bit_rate = quantized_streams[current_bone_index].rotations.get_bit_rate();
					bool can_increase_rotation_precision = is_rotation_variable && rotation_bit_rate < HIGHEST_BIT_RATE;
					if (can_increase_rotation_precision && error_per_stream[current_bone_index].rotation > worst_track_error)
					{
						target_bone_index = current_bone_index;
						worst_track_error = error_per_stream[current_bone_index].rotation;
						target_track_type = AnimationTrackType8::Rotation;
					}

					uint8_t translation_bit_rate = quantized_streams[current_bone_index].translations.get_bit_rate();
					bool can_increase_translation_precision = is_translation_variable && translation_bit_rate < HIGHEST_BIT_RATE;
					if (can_increase_translation_precision && error_per_stream[current_bone_index].translation > worst_track_error)
					{
						target_bone_index = current_bone_index;
						worst_track_error = error_per_stream[current_bone_index].translation;
						target_track_type = AnimationTrackType8::Translation;
					}

					const RigidBone& bone = skeleton.get_bone(current_bone_index);
					current_bone_index = bone.parent_index;
				}

				if (target_bone_index == INVALID_BONE_INDEX)
				{
					// Failed to find a target stream that we could increase its precision
					// This is bad, we have a bone with an error above the error threshold,
					// but every bone in the hierarchy leading up to it is at full precision.
					// Bail out

					// In practice, this should only ever happen if rotations or translations are quantized
					// to a fixed format with yields high loss while the other tracks are variable.
					// They will attempt to keep as much precision as possible but ultimately fail.
					// Variable precision works best if all tracks are variable.

					bitset_set(low_resolution_bones, bitset_size, bad_bone_index, true);
					continue;
				}

				// Increase its bit rate a bit
				if (target_track_type == AnimationTrackType8::Rotation)
				{
					uint8_t new_bit_rate = quantized_streams[target_bone_index].rotations.get_bit_rate() + 1;
					quantize_fixed_rotation_stream(allocator, bone_streams[target_bone_index].rotations, new_bit_rate, quantized_streams[target_bone_index].rotations);
				}
				else
				{
					uint8_t new_bit_rate = quantized_streams[target_bone_index].translations.get_bit_rate() + 1;
					quantize_fixed_translation_stream(allocator, bone_streams[target_bone_index].translations, new_bit_rate, quantized_streams[target_bone_index].translations);
				}
			}

#if 0
			error = 0.0f;
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
				error = max(error, error_per_bone[bone_index]);
			printf("DUMPED ERROR: %f\n", error);
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				const BoneStreams& bone_stream = quantized_streams[bone_index];
				if (bone_stream.is_rotation_animated())
					printf("DUMPED R RATE: %u\n", bone_stream.rotations.get_bit_rate());
				if (bone_stream.is_translation_animated())
					printf("DUMPED T RATE: %u\n", bone_stream.translations.get_bit_rate());
			}
#endif

			// Swap our streams
			for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				std::swap(bone_streams[bone_index], quantized_streams[bone_index]);
			}

			deallocate_type_array(allocator, quantized_streams, num_bones);
			deallocate_type_array(allocator, raw_local_pose, num_bones);
			deallocate_type_array(allocator, lossy_local_pose, num_bones);
			deallocate_type_array(allocator, error_per_bone, num_bones);
			deallocate_type_array(allocator, error_per_stream, num_bones);
			deallocate_type_array(allocator, low_resolution_bones, bitset_size);
		}
	}

	inline void quantize_streams(Allocator& allocator, BoneStreams* bone_streams, uint16_t num_bones, RotationFormat8 rotation_format, VectorFormat8 translation_format, const AnimationClip& clip, const RigidSkeleton& skeleton)
	{
		const bool is_rotation_variable = is_rotation_format_variable(rotation_format);
		const bool is_translation_variable = is_vector_format_variable(translation_format);

		if (is_rotation_variable || is_translation_variable)
		{
			impl::quantize_variable_streams(allocator, bone_streams, num_bones, rotation_format, translation_format, clip, skeleton);
		}
		else
		{
			if (!is_rotation_variable)
				impl::quantize_fixed_rotation_streams(allocator, bone_streams, num_bones, rotation_format, false);

			if (!is_translation_variable)
				impl::quantize_fixed_translation_streams(allocator, bone_streams, num_bones, translation_format);
		}
	}
}
