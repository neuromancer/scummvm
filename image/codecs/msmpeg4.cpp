/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * MP43-only port of FFmpeg n2.8, adapted for ScummVM on 2026-10-13.
 * Uses ScummVM streams, surfaces and YUV conversion, a bounded VLC reader,
 * and a scalar IDCT with widened accumulators.
 * https://github.com/FFmpeg/FFmpeg/tree/n2.8/libavcodec
 *
 * Original copyright notices:
 *
 * msmpeg4dec.c:
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2013 Michael Niedermayer <michaelni@gmx.at>
 *
 * msmpeg4.c:
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * mpeg4videodec.c:
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2010 Michael Niedermayer <michaelni@gmx.at>
 *
 * h263.c:
 * Copyright (c) 2000,2001 Fabrice Bellard
 * H263+ support.
 * Copyright (c) 2001 Juan J. Sierralta P
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * mpegvideo_motion.c:
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * simple_idct_template.c:
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
 * Based upon commented-out C code from mpeg2dec's idct_mmx.c,
 * written by Aaron Holtzman <aholtzma@ess.engr.uvic.ca>.
 *
 * The upstream files are LGPL-2.1-or-later. This adapted copy uses the
 * GPL-3.0-or-later terms above, as permitted by LGPL-2.1 section 3.
 */

#include "image/codecs/msmpeg4.h"
#include "image/codecs/msmpeg4_tables.h"
#include "common/array.h"
#include "common/bitstream.h"
#include "common/textconsole.h"
#include "graphics/yuv_to_rgb.h"

namespace Image {
namespace MSMPEG4 {

typedef Common::BitStream8MSB Bits;

// A bounded VLC reader. Invalid/truncated codes fail the frame rather than
// terminating the engine through Common::Huffman's invalid-code error().
class VLC {
	struct Node {
		int child[2] = {-1, -1};
		int symbol = -1;
	};
	Common::Array<Node> _nodes;
public:
	void add(uint32 code, uint length, int symbol) {
		if (_nodes.empty())
			_nodes.push_back(Node());
		int node = 0;
		for (int bit = length - 1; bit >= 0; --bit) {
			int side = (code >> bit) & 1;
			int next = _nodes[node].child[side];
			if (next < 0) {
				next = _nodes.size();
				_nodes[node].child[side] = next;
				_nodes.push_back(Node());
			}
			node = next;
		}
		_nodes[node].symbol = symbol;
	}
	template<class T, uint N> void init(const T (&table)[N][2]) {
		for (uint i = 0; i < N; ++i)
			add(table[i][0], table[i][1], i);
	}
	int read(Bits &bits) const {
		int node = 0;
		for (int i = 0; i < 32; ++i) {
			if (_nodes[node].symbol >= 0)
				return (bits.pos() > bits.size()) ? -1 : _nodes[node].symbol;
			node = _nodes[node].child[bits.getBit()];
			if (node < 0 || (bits.pos() > bits.size()))
				return -1;
		}
		return -1;
	}
};

struct RunLevel {
	VLC vlc;
	int count, last;
	const int8 *run, *level;
	byte maxLevel[2][64] = {};
	byte maxRun[2][128] = {};
	template<uint N> void init(const uint16 (&codes)[N][2], const int8 *runs, const int8 *levels, int firstLast) {
		vlc.init(codes);
		count = N - 1;
		last = firstLast;
		run = runs;
		level = levels;
		for (int i = 0; i < count; ++i) {
			int l = i >= last;
			maxLevel[l][run[i]] = MAX<int>(maxLevel[l][run[i]], level[i]);
			maxRun[l][level[i]] = MAX<int>(maxRun[l][level[i]], run[i]);
		}
	}
};

struct Tables {
	VLC intra, inter, dc[2][2], motion[2];
	RunLevel rl[6];
	Tables() {
		intra.init(ff_msmp4_mb_i_table);
		inter.init(ff_table_mb_non_intra);
		dc[0][0].init(ff_table0_dc_lum);
		dc[0][1].init(ff_table0_dc_chroma);
		dc[1][0].init(ff_table1_dc_lum);
		dc[1][1].init(ff_table1_dc_chroma);
		for (int i = 0; i < 1100; ++i) {
			motion[0].add(table0_mv_code[i], table0_mv_bits[i], i);
			motion[1].add(table1_mv_code[i], table1_mv_bits[i], i);
		}
		rl[0].init(table0_vlc, table0_run, table0_level, 85);
		rl[1].init(table2_vlc, table2_run, table2_level, 119);
		rl[2].init(ff_mpeg4_intra_vlc, ff_mpeg4_intra_run, ff_mpeg4_intra_level, 67);
		rl[3].init(table1_vlc, table1_run, table1_level, 81);
		rl[4].init(table4_vlc, table4_run, table4_level, 99);
		rl[5].init(ff_inter_vlc, ff_inter_run, ff_inter_level, 58);
	}
};

// FFmpeg's 8-bit simple IDCT, using wide accumulators to avoid signed
// overflow on damaged input. Rows retain the original int16 intermediate.
static void idctPass(const int16 *src, int stride, int64 out[8], bool column) {
	const int w1 = 22725, w2 = 21407, w3 = 19266, w4 = 16383;
	const int w5 = 12873, w6 = 8867, w7 = 4520;
	int64 a0 = int64(w4) * (src[0] + (column ? 32 : 0)) + (column ? 0 : 1024);
	int64 a1 = a0, a2 = a0, a3 = a0;
	a0 += int64(w2) * src[2 * stride] + int64(w4) * src[4 * stride] + int64(w6) * src[6 * stride];
	a1 += int64(w6) * src[2 * stride] - int64(w4) * src[4 * stride] - int64(w2) * src[6 * stride];
	a2 += -int64(w6) * src[2 * stride] - int64(w4) * src[4 * stride] + int64(w2) * src[6 * stride];
	a3 += -int64(w2) * src[2 * stride] + int64(w4) * src[4 * stride] - int64(w6) * src[6 * stride];
	int64 b0 = int64(w1) * src[stride] + int64(w3) * src[3 * stride] + int64(w5) * src[5 * stride] + int64(w7) * src[7 * stride];
	int64 b1 = int64(w3) * src[stride] - int64(w7) * src[3 * stride] - int64(w1) * src[5 * stride] - int64(w5) * src[7 * stride];
	int64 b2 = int64(w5) * src[stride] - int64(w1) * src[3 * stride] + int64(w7) * src[5 * stride] + int64(w3) * src[7 * stride];
	int64 b3 = int64(w7) * src[stride] - int64(w5) * src[3 * stride] + int64(w3) * src[5 * stride] - int64(w1) * src[7 * stride];
	int shift = column ? 20 : 11;
	out[0] = (a0 + b0) >> shift; out[7] = (a0 - b0) >> shift;
	out[1] = (a1 + b1) >> shift; out[6] = (a1 - b1) >> shift;
	out[2] = (a2 + b2) >> shift; out[5] = (a2 - b2) >> shift;
	out[3] = (a3 + b3) >> shift; out[4] = (a3 - b3) >> shift;
}

static void idct(int16 block[64], byte *dst, int pitch, bool add) {
	for (int y = 0; y < 8; ++y) {
		int16 *row = block + y * 8;
		if (!(row[1] | row[2] | row[3] | row[4] | row[5] | row[6] | row[7])) {
			int16 dc = row[0] * 8;
			for (int x = 0; x < 8; ++x)
				row[x] = dc;
		} else {
			int64 out[8];
			idctPass(row, 1, out, false);
			for (int x = 0; x < 8; ++x)
				row[x] = out[x];
		}
	}
	for (int x = 0; x < 8; ++x) {
		int64 out[8];
		idctPass(block + x, 8, out, true);
		for (int y = 0; y < 8; ++y) {
			byte &pixel = dst[y * pitch + x];
			pixel = CLIP<int64>(out[y] + (add ? pixel : 0), 0, 255);
		}
	}
}

static int decode012(Bits &bits) {
	return bits.getBit() ? 1 + bits.getBit() : 0;
}

struct Predictor {
	int dc = 1024;
	int16 ac[16] = {};
	byte coded = 0;
};

static int median(int a, int b, int c) {
	return a + b + c - MIN(a, MIN(b, c)) - MAX(a, MAX(b, c));
}

} // namespace MSMPEG4

using namespace MSMPEG4;

struct MSMPEG4Decoder::Context {
	Tables tables;
	int width, height, mbWidth, mbHeight;
	int pitch[3], predPitch[3];
	Common::Array<byte> current[3], previous[3];
	Common::Array<Predictor> predictors[3];
	Common::Array<Common::Point> motion;
	bool haveReference = false, noRounding = false, flipRounding = false;
	int sliceHeight, quant, rlIndex, chromaIndex, dcIndex, mvIndex;
	bool intraFrame, useSkip;

	Context(int w, int h) : width(w), height(h), mbWidth((w + 15) / 16), mbHeight((h + 15) / 16) {
		sliceHeight = mbHeight;
		for (int p = 0; p < 3; ++p) {
			int size = p ? 8 : 16;
			pitch[p] = mbWidth * size;
			predPitch[p] = mbWidth * (p ? 1 : 2) + 2;
			current[p].resize(pitch[p] * mbHeight * size, 0);
			previous[p].resize(current[p].size(), 0);
			predictors[p].resize(predPitch[p] * (mbHeight * (p ? 1 : 2) + 1));
		}
		motion.resize((mbWidth + 2) * (mbHeight + 1));
	}

	bool decodeBlock(Bits &bits, int16 block[64], int n, bool intra, bool coded, bool acPrediction, int mx, int my) {
		int plane = n < 4 ? 0 : n - 3;
		int x = mx * (plane ? 1 : 2) + (plane ? 0 : n & 1) + 1;
		int y = my * (plane ? 1 : 2) + (plane ? 0 : n >> 1) + 1;
		Predictor *pred = &predictors[plane][y * predPitch[plane] + x];
		int direction = 0, scale = 0;
		const byte *scan = kZigzag;
		RunLevel &rl = tables.rl[intra ? (plane ? 3 + chromaIndex : rlIndex) : 3 + rlIndex];
		if (intra) {
			int dc = tables.dc[dcIndex][plane != 0].read(bits);
			if (dc < 0)
				return false;
			bool escape = dc == 119;
			if (escape)
				dc = bits.getBits(8);
			if ((escape || dc) && bits.getBit())
				dc = -dc;
			scale = plane ? ff_mpeg4_c_dc_scale_table[quant] : ff_mpeg4_y_dc_scale_table[quant];
			int a = pred[-1].dc, b = pred[-1 - predPitch[plane]].dc, c = pred[-predPitch[plane]].dc;
			if (my % sliceHeight == 0 && (n & 2) == 0)
				b = c = 1024;
			a = (a + scale / 2) / scale;
			b = (b + scale / 2) / scale;
			c = (c + scale / 2) / scale;
			direction = ABS(a - b) <= ABS(b - c);
			dc += direction ? c : a;
			if (dc < 0 || dc > 256 * scale)
				return false;
			pred->dc = dc * scale;
			block[0] = dc;
			if (acPrediction)
				scan = direction ? ff_alternate_horizontal_scan : ff_alternate_vertical_scan;
		}
		int index = intra ? 0 : -1;
		if (coded) {
			for (int coefficient = 0; coefficient < 64; ++coefficient) {
				int symbol = rl.vlc.read(bits);
				if (symbol < 0)
					return false;
				int run, level;
				bool last;
				if (symbol == rl.count) {
					int escape = bits.getBit() ? 1 : (bits.getBit() ? 2 : 3);
					if (escape == 3) {
						last = bits.getBit();
						run = bits.getBits(6);
						level = (int8)bits.getBits(8);
					} else {
						symbol = rl.vlc.read(bits);
						if (symbol < 0 || symbol == rl.count)
							return false;
						last = symbol >= rl.last;
						run = rl.run[symbol];
						level = rl.level[symbol];
						if (escape == 1)
							level += rl.maxLevel[last][run];
						else
							run += rl.maxRun[last][level] + (intra ? 0 : 1);
						if (bits.getBit())
							level = -level;
					}
				} else {
					last = symbol >= rl.last;
					run = rl.run[symbol];
					level = rl.level[symbol];
					if (bits.getBit())
						level = -level;
				}
				index += run + 1;
				if (index >= 64 || (bits.pos() > bits.size()))
					return false;
				if (!intra)
					level = level * quant * 2 + (level > 0 ? 1 : -1) * ((quant - 1) | 1);
				block[scan[index]] = level;
				if (last)
					break;
				if (index == 63)
					return false;
			}
		}
		if (intra) {
			if (acPrediction) {
				const Predictor &neighbor = pred[direction ? -predPitch[plane] : -1];
				for (int i = 1; i < 8; ++i)
					block[direction ? i : i * 8] += neighbor.ac[direction ? i + 8 : i];
			}
			for (int i = 1; i < 8; ++i) {
				pred->ac[i] = block[i * 8];
				pred->ac[8 + i] = block[i];
			}
			block[0] *= scale;
			for (int i = 1; i < 64; ++i)
				if (block[i])
					block[i] = block[i] * quant * 2 + (block[i] > 0 ? 1 : -1) * ((quant - 1) | 1);
		} else {
			*pred = Predictor();
		}
		int pixelX = mx * (plane ? 8 : 16) + (plane ? 0 : (n & 1) * 8);
		int pixelY = my * (plane ? 8 : 16) + (plane ? 0 : (n >> 1) * 8);
		if (intra || coded)
			idct(block, current[plane].data() + pixelY * pitch[plane] + pixelX, pitch[plane], !intra);
		return (bits.pos() <= bits.size());
	}

	void compensate(int mx, int my, int vx, int vy) {
		for (int p = 0; p < 3; ++p) {
			int size = p ? 8 : 16;
			// H.263 chroma uses a half pixel whenever either discarded luma bit is set.
			int dx = p ? (vx >> 2) : (vx >> 1);
			int dy = p ? (vy >> 2) : (vy >> 1);
			bool halfX = p ? (vx & 3) != 0 : (vx & 1) != 0;
			bool halfY = p ? (vy & 3) != 0 : (vy & 1) != 0;
			int edgeX = pitch[p] - 1;
			int edgeY = mbHeight * size - 1;
			for (int y = 0; y < size; ++y) {
				int sy = my * size + y + dy;
				int y0 = CLIP(sy, 0, edgeY), y1 = CLIP(sy + 1, 0, edgeY);
				byte *dst = current[p].data() + (my * size + y) * pitch[p] + mx * size;
				for (int x = 0; x < size; ++x) {
					int sx = mx * size + x + dx;
					int x0 = CLIP(sx, 0, edgeX), x1 = CLIP(sx + 1, 0, edgeX);
					int a = previous[p][y0 * pitch[p] + x0];
					if (halfX && halfY) {
						int b = previous[p][y0 * pitch[p] + x1];
						int c = previous[p][y1 * pitch[p] + x0];
						int d = previous[p][y1 * pitch[p] + x1];
						dst[x] = (a + b + c + d + 2 - noRounding) >> 2;
					} else if (halfX || halfY) {
						int b = previous[p][(halfY ? y1 : y0) * pitch[p] + (halfX ? x1 : x0)];
						dst[x] = (a + b + 1 - noRounding) >> 1;
					} else {
						dst[x] = a;
					}
				}
			}
		}
	}

	bool decode(Bits &bits) {
		int type = bits.getBits(2);
		if (type > 1 || (type == 1 && !haveReference))
			return false;
		intraFrame = type == 0;
		quant = bits.getBits(5);
		if (!quant)
			return false;
		if (intraFrame) {
			int slices = (int)bits.getBits(5) - 22;
			if (slices <= 0 || slices > mbHeight)
				return false;
			sliceHeight = mbHeight / slices;
			chromaIndex = decode012(bits);
			rlIndex = decode012(bits);
			dcIndex = bits.getBit();
			noRounding = true;
		} else {
			useSkip = bits.getBit();
			rlIndex = chromaIndex = decode012(bits);
			dcIndex = bits.getBit();
			mvIndex = bits.getBit();
			noRounding = flipRounding ? !noRounding : false;
		}
		for (int p = 0; p < 3; ++p)
			for (uint i = 0; i < predictors[p].size(); ++i)
				predictors[p][i] = Predictor();
		for (uint i = 0; i < motion.size(); ++i)
			motion[i] = Common::Point();
		for (int my = 0; my < mbHeight; ++my) {
			if (my && my % sliceHeight == 0)
				for (int p = 0; p < 3; ++p)
					for (uint i = 0; i < predictors[p].size(); ++i)
						memset(predictors[p][i].ac, 0, sizeof(predictors[p][i].ac));
			for (int mx = 0; mx < mbWidth; ++mx) {
				if ((bits.pos() > bits.size()))
					return false;
				bool skip = !intraFrame && useSkip && bits.getBit();
				bool intra = intraFrame, ac = false;
				int cbp = 0, vx = 0, vy = 0;
				Common::Point *mv = &motion[(my + 1) * (mbWidth + 2) + mx + 1];
				if (!skip) {
					int code = (intraFrame ? tables.intra : tables.inter).read(bits);
					if (code < 0)
						return false;
					cbp = code & 63;
					if (intraFrame) {
						for (int n = 0; n < 4; ++n) {
							Predictor *pred = &predictors[0][(my * 2 + (n >> 1) + 1) * predPitch[0] + mx * 2 + (n & 1) + 1];
							int a = pred[-1].coded, b = pred[-predPitch[0] - 1].coded, c = pred[-predPitch[0]].coded;
							int val = ((code >> (5 - n)) & 1) ^ (b == c ? a : c);
							pred->coded = val;
							cbp = (cbp & ~(1 << (5 - n))) | (val << (5 - n));
						}
					} else {
						intra = !(code & 64);
					}
					if (intra) {
						ac = bits.getBit();
					} else {
						Common::Point a = mv[-1], b = mv[-mbWidth - 2], c = mv[-mbWidth - 1];
						vx = my % sliceHeight == 0 ? a.x : median(a.x, b.x, c.x);
						vy = my % sliceHeight == 0 ? a.y : median(a.y, b.y, c.y);
						int symbol = tables.motion[mvIndex].read(bits);
						if (symbol < 0)
							return false;
						if (symbol == 1099) {
							vx += bits.getBits(6) - 32;
							vy += bits.getBits(6) - 32;
						} else {
							vx += (mvIndex ? table1_mvx : table0_mvx)[symbol] - 32;
							vy += (mvIndex ? table1_mvy : table0_mvy)[symbol] - 32;
						}
						if (vx <= -64) vx += 64; else if (vx >= 64) vx -= 64;
						if (vy <= -64) vy += 64; else if (vy >= 64) vy -= 64;
					}
				}
				*mv = Common::Point(vx, vy);
				if (!intra)
					compensate(mx, my, vx, vy);
				for (int n = 0; n < 6; ++n) {
					int16 block[64] = {};
					if (!decodeBlock(bits, block, n, intra, (cbp >> (5 - n)) & 1, ac, mx, my)) {
						warning("MP43: invalid block at %d,%d/%d (bit %u)", mx, my, n, bits.pos());
						return false;
					}
				}
			}
		}
		if (intraFrame) {
			uint left = bits.size() - bits.pos();
			flipRounding = false;
			if (left >= 17 && left < 25) {
				bits.skip(16); // nominal frame rate and bit rate
				flipRounding = bits.getBit();
			}
		}
		return (bits.pos() <= bits.size());
	}
};

MSMPEG4Decoder::MSMPEG4Decoder(int width, int height) : _context(nullptr), _format(getDefaultYUVFormat()) {
	// Limit allocation and reject dimensions unsupported by the YUV420 converter.
	if (width > 0 && height > 0 && width <= 4096 && height <= 4096 && !(width & 1) && !(height & 1))
		_context = new Context(width, height);
}

MSMPEG4Decoder::~MSMPEG4Decoder() {
	delete _context;
	_surface.free();
}

bool MSMPEG4Decoder::setOutputPixelFormat(const Graphics::PixelFormat &format) {
	if (format.bytesPerPixel != 2 && format.bytesPerPixel != 4)
		return false;
	_format = format;
	_surface.free();
	return true;
}

const Graphics::Surface *MSMPEG4Decoder::decodeFrame(Common::SeekableReadStream &stream) {
	if (!_context)
		return nullptr;
	Bits bits(stream);
	if (!_context->decode(bits)) {
		warning("MP43: corrupt or unsupported frame");
		_context->haveReference = false;
		return nullptr;
	}
	if (!_surface.getPixels())
		_surface.create(_context->width, _context->height, _format);
	YUVToRGBMan.convert420(&_surface, Graphics::YUVToRGBManager::kScaleITU,
		_context->current[0].data(), _context->current[1].data(), _context->current[2].data(),
		_context->width, _context->height, _context->pitch[0], _context->pitch[1]);
	for (int p = 0; p < 3; ++p)
		_context->current[p].swap(_context->previous[p]);
	_context->haveReference = true;
	return &_surface;
}

} // namespace Image
