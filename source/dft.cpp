﻿#include "../include/dft.h"

#include "../include/log.h"

namespace Steinberg {
namespace HelloWorld {

DFT::DFT(size_t num, double sampleRate) {
	this->num = num;
	setSampleRate(sampleRate);

	f0		  = 0.0;
	noise_level = 0.0f;

	// 110 ~ 1760 Hzの間を対象とする

	// 周波数分解能からアプローチ
	// 110Hz(A2) の半音上 A#2 (116.54) の更に半分の3.27Hzを周波数分解能にしたい
	// Δf = fs / N https://www.onosokki.co.jp/HP-WK/c_support/faq/fft_common/fft_analys_4.htm
	// サンプリング周波数 44100 Hzの場合 N = 13486
	// サンプリング周波数 48000 Hzの場合 N = 14678
	// よって N = 16384 (2^14) ディレイは、372msec

	// ディレイからアプローチする
	// 音は2m進む間に約5msec遅れる。ざっくりディレイ 20msec以内にしたい
	// Δt = N / fs
	// N = 1024, 分解能は 43Hz(44100), 46.8(48000)

	// バランスとると4096かなぁ

	re = new float[num];
	im = new float[num];

	spectrum = new float[MaxF0Range];
	for (int i = 0; i < MaxF0Range; i++) {
		spectrum[i] = 0.0f;
	}

	fpeak		= new Point[64];
	fpeak[0].index = 0;
	fpeak[0].value = noise_level;
	fpeak[1].index = rangeF0Search;
	fpeak[1].value = noise_level;
	fpeakCount	= 2;

	int p = 0;
	for (int i = 0; i < sizeof(int) * 8; i++) {
		if ((num & (1 << i)) > 0) {
			p += i + 1;
		}
	}

	index = new int[num];
	for (int i = 0; i < num; i++) {
		index[i] = 0;
		int t    = i;
		for (int j = 1; j < p; j++) {
			index[i] <<= 1;
			if ((t & 1) == 1) index[i] |= 1;
			t >>= 1;
		}
	}

	double r = 6.283185307179586476925286766559 / num;

	int mask1 = (num - 1) >> 1;
	int mask2 = (num - 1) >> 2;
	int mask0 = (num - 1) ^ mask1;

	w_re = new float[num / 2];
	w_im = new float[num / 2];
	for (int i = 0; i < num / 2; i++) {
		if ((i & mask1) == 0) {
			w_re[i] = (i & mask0) > 0 ? -1.0f : 1.0f;
			w_im[i] = 0.0f;
		} else if ((i & mask2) == 0) {
			w_re[i] = 0.0f;
			w_im[i] = (i & mask0) > 0 ? -1.0f : 1.0f;
		} else {
			double phase = i * r;

			w_re[i] = cos(phase);
			w_im[i] = sin(phase);
		}
	}
}

DFT::~DFT() {
	delete[] re;
	delete[] im;
	delete[] spectrum;

	delete[] index;
	delete[] w_re;
	delete[] w_im;
}

tresult DFT::setSampleNum(size_t num) { return calculateF0Range(num, sampleRate); }
tresult DFT::setSampleRate(double sampleRate) { return calculateF0Range(num, sampleRate); }
tresult DFT::calculateF0Range(size_t num, double sampleRate) {
	size_t prevNum		  = this->num;
	double prevSampleRate = this->sampleRate;

	this->num		  = num;
	this->sampleRate = sampleRate;

	this->rangeF0Search = 1381 * this->num / sampleRate;  // 8192samp, 44100Hzの時に256
	/**
	 * | num | rate | f0 | resolution [Hz]
	 * | 8192| 48000| 235|  5.86
	 * | 4096|      | 117| 11.72
	 * | 2048|      |  58| 23.44
	 * | 1024|      |  29| 46.88
	 * |  512|      |  14| 93.75
	 * | 8192| 44100| 256|  5.38
	 * | 4096|      | 128| 10.77
	 * | 2048|      |  64| 21.53
	 * | 1024|      |  32| 43.07
	 * |  512|      |  16| 86.13
	 * 
	 * 
	 */

	if (this->rangeF0Search == 0) {
		calculateF0Range(prevNum, prevSampleRate);
		return kResultFalse;
	}
	if (this->rangeF0Search > MaxF0Range) this->rangeF0Search = MaxF0Range;
	return kResultOk;
}

void DFT::process(float* source) {
	memcpy(re, source, sizeof(float) * num);
	memset(im, 0, sizeof(float) * num);

	for (int ns = num / 2; ns > 0; ns /= 2) {
		int arg = 0;
		for (int j = 0; j < num; j += 2 * ns) {
			float* _w_re = w_re + arg;
			float* _w_im = w_im + arg;

			for (int i = j; i < j + ns; i++) {
				int i1 = i + ns;

				float _r = re[i1] * (*_w_re) - im[i1] * (*_w_im);
				float _i = re[i1] * (*_w_im) + im[i1] * (*_w_re);

				re[i1] = re[i] - _r;
				im[i1] = im[i] - _i;

				re[i] += _r;
				im[i] += _i;
			}

			int k = num / 4;
			while (k <= arg && k > 0) {
				arg -= k;
				k /= 2;
			}
			arg += k;
		}
	}

	// 1 - 256 で 0(DC) - 1389.312Hz (44100), 1512.183Hz (44800)
	for (int i = 1; i <= rangeF0Search; i++) {
		int k   = index[i];
		float p = re[k] * re[k] + im[k] * im[k];

		spectrum[i - 1] = logf(p * i);
	}

	// F0推定
	// 包括線を書く
	fpeak[1].index = rangeF0Search;
	fpeak[0].value = fpeak[1].value = noise_level;
	fpeakCount				  = 2;

	while (fpeakCount < 64) {
		fpeak[fpeakCount].value = 0.0f;
		fpeak[fpeakCount].index = 0;

		float a = (fpeak[fpeakCount - 1].value - fpeak[0].value) / fpeak[fpeakCount - 1].index;
		for (size_t i = fpeak[0].index + 1; i < fpeak[fpeakCount - 1].index - 1; i++) {
			float y  = fpeak[0].value + a * i;
			float dy = spectrum[i] - y;
			if (dy > fpeak[fpeakCount].value) {
				fpeak[fpeakCount].value = dy;
				fpeak[fpeakCount].index = i;
			}
		}
		if (fpeak[fpeakCount].index > 0) {
			fpeak[fpeakCount].value = spectrum[fpeak[fpeakCount].index];
			fpeakCount++;
		} else
			break;
	}
	f0 = fpeak[fpeakCount - 1].index;
}

}  // namespace HelloWorld
}  // namespace Steinberg