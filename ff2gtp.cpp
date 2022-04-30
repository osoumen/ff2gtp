//
//  ff2gtp.cpp
//  ff2gtp
//
//  Created by osoumen on 2022/04/30.
//

#include <list>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include "crc32.h"

#if _WIN32
#define DIRECTORY_SEPARATOR_CHAR '\\'
#else
#define DIRECTORY_SEPARATOR_CHAR '/'
#endif

#define USERPATCH_FORMAT_VERSION	(1)

bool flatten_TL_option = true;

#define CHUNKID(a, b, c, d) \
((((int)a) << 0) | (((int)b) << 8) | (((int)c) << 16) | (((int)d) << 24))

#define		_GIMIC_SW_VER_MAJOR		(7)
#define		_GIMIC_SW_VER_MINOR		(2)
#define		_GIMIC_SW_VER_REV		('a')

#define		_GIMIC_SW_VER_SVNREV	(0)
#define		_GIMIC_SW_VER_BUILD		(0)

#define		_GIMIC_MIDIFW_VER_MAJOR	(1)
#define		_GIMIC_MIDIFW_VER_MINOR	(1)

#define		GIMIC_DEVICETYPE_SOUNDMODULE		(0x0000)
#define		GIMIC_MOTHERBOARD_ID_TABLEREV		(0x0004)	// HWREVを変更したら+1する
#define		GIMIC_SOUNDMODULE_TABLEREV				(GIMIC_DEVICETYPE_SOUNDMODULE | 0x000A)	// 変更したら+1する

static const int PATCHDATA_SIZE = 128;
const int SIGNATURE_BYTES = 16;

const char modulemidiMagicData[SIGNATURE_BYTES] = "GMCMIDIModule01";

typedef struct {
	char		sig[8];	// "GMCTIMB" gimic timble parameter
	uint32_t	chunk_start_pos;	// チャンクデータのファイル内開始位置
	uint8_t		mb_fw_version[4];	// _GIMIC_SW_VER_MAJOR, _GIMIC_SW_VER_MINOR, _GIMIC_SW_VER_SVNREV, _GIMIC_SW_VER_BUILD
	uint16_t	mb_type_tablerev;	// GIMIC_MOTHERBOARD_ID_TABLEREV
	uint16_t	soundmodule_tablerev;	// GIMIC_SOUNDMODULE_TABLEREV
	uint16_t	mb_type_id;	// GIMIC_MOTHERBOARD_ID_MB{1,2PRO,2STD,2LT}
	uint16_t	soundmodule_id;	// 実際に発音するのに使用されているモジュール
	char		mb_serial_no[8];	// MBシリアル番号
} PatchFileHeader;
typedef struct {
	uint32_t	chunk_type;	// データのタイプ
	uint32_t	chunk_size;	// データの中身のバイト数
	uint32_t	chunk_crc;	// データの中身のCRC32
//		uint8_t		chunk_data[];	// データの中身
} PatchFileChunk;

static const uint32_t chunk_type_raw_patch = CHUNKID('r','p','t','c');
static const int patch_file_header_size = sizeof(PatchFileHeader);
static const int filepath_buffer_param_index = 1;
static const int8_t patch_type_not_available = 0xff;

PatchFileHeader	file_header = {
	"GMCTIMB",
	sizeof(PatchFileHeader),
	_GIMIC_SW_VER_MAJOR,
	_GIMIC_SW_VER_MINOR,
	_GIMIC_SW_VER_SVNREV,
	_GIMIC_SW_VER_BUILD,
	GIMIC_MOTHERBOARD_ID_TABLEREV,
	GIMIC_SOUNDMODULE_TABLEREV,
};

const char sGmcPatchFileSigatute[] = "GMCTIMB";

const char sUserPatch[][13] = {
	"patchbnk.000",
	"patchbnk.001",
};

//! エンベロープ、LFOのゲイン調整に使える入力ソース
enum EnvelopeInputSelect {
	kModulatorInput_NoInput = 0,
	kModulatorInput_Max,
	kModulatorInput_NonConstStart,
	kModulatorInput_Velocity = kModulatorInput_NonConstStart,
	kModulatorInput_ChAfterTouch,
	kModulatorInput_KeyScale,
	kModulatorInput_ModWheel,
	kModulatorInput_BreathController,
	kModulatorInput_FootController,
	kModulatorInput_Balance,
	kModulatorInput_General1,
	kModulatorInput_General2,
	kModulatorInput_General3,
	kModulatorInput_General4,
	kModulatorInput_SoundVariation,
	kModulatorInput_Timbre,
	kModulatorInput_TremoloDepth,
	kModulatorInput_InputNum = (kModulatorInput_TremoloDepth + 1) - kModulatorInput_NonConstStart,
};

//! パッチの種別を表す先頭1バイト
enum DriverPatchType {
	kPatchTypeUndefined	= 0x00,
	
	kPatchTypeOPM_FM	= 0x01,
	
	kPatchTypeOPN_FM	= 0x02,
	kPatchTypeOPN_FMch3	= 0x03,
	kPatchTypeOPN_SSG	= 0x04,
	kPatchTypeOPN_RHYTHM= 0x05,
	kPatchTypeOPNA_ADPCM= 0x06,
	
	kPatchTypeOPL3_FM2op= 0x07,
	kPatchTypeOPL3_FM4op= 0x08,
	kPatchTypeOPL3_RHYTHM= 0x09,
	
	kPatchTypeSPC_PCM	= 0x0a,
	kPatchTypeOPLL_FM	= 0x0b,
	kPatchTypeOPLL_RHYTHM= 0x0c,

	kPatchTypeNum,
	
	kPatchTypeProgram = 0x1f
};

typedef union FfPatch {
	struct {
		uint8_t		data[32];
	} raw;
	struct {
		uint8_t		dt_mul[4];
		uint8_t		tl[4];
		uint8_t		ks_ar[4];
		uint8_t		ame_dr[4];
		uint8_t		sr[4];
		uint8_t		sl_rr[4];
		uint8_t		fb_con;
		uint8_t		patch_name[7];
	} named;
} FfPatch;

typedef union FfopmPatch {
	struct {
		uint8_t		data[32];
	} raw;
	struct {
		uint8_t		dt1_mul[4];
		uint8_t		tl[4];
		uint8_t		ks_ar[4];
		uint8_t		ame_d1r[4];
		uint8_t		dt2_d2r[4];
		uint8_t		d1l_rr[4];
		uint8_t		fl_con;
		uint8_t		patch_name[7];
	} named;
} FfopmPatch;

//! パッチデータの共通部分(20Byte)
typedef struct PatchCommon {
	uint8_t		patch_type;		///< 音源種別
	uint8_t		lk_nxp_format_version;	///< 1.変更不可 1.エクスポート不可 6.フォーマットバージョン(後方互換性用)
	char		name[14];		///< パッチ名
	uint32_t	original_clock;	///< 作成時のクロック周波数
} PatchCommon;

// LFOのパラメータ(4Byte)
typedef struct LFOParam {
	uint8_t midisync_wf_inputselect;		// 1.3.4
	uint8_t	keyonrst_inputdepth;	// 1.7
	uint8_t freq;
	uint8_t	fadein;
} LFOParam;

// ソフトエンベロープのパラメータ(12Byte)
typedef struct EnvelopeParam {
	uint8_t inputselect;
	int8_t	inputdepth;
	uint8_t	attack_time;
	uint8_t	attack_slope;
	
	uint8_t hold_time;
	uint8_t decay_time;
	uint8_t decay_release_slope;
	uint8_t sustain_level;
	
	uint8_t release_time;
	uint8_t release_level;
	int8_t	key_scaling;		// 最大時に1oct上がる毎にnote=60を基準に半分に
	int8_t	velocity_scaling;	// 80を基準として32上がる毎に倍に
} EnvelopeParam;

// キースケールのカーブ定義(4Byte)
typedef struct KeyScaleParam {
	int8_t	min_level;
	uint8_t	slope1_2;
	int8_t	center_key;
	int8_t	max_level;
} KeyScaleParam;

static const int SWLFO_NUM = 2;
static const int SWENV_NUM = 2;

typedef struct TonedSynthCommon {
	int8_t			transpose;
	int8_t			tuning;
	int8_t			panpot;
	int8_t			panpot_ksl_sens;
	
	int8_t			pitch_lfo_sens[SWLFO_NUM];
	int8_t			pitch_env_sens[SWENV_NUM];
//	int8_t			panpot_lfo_sens[SWLFO_NUM];
//	int8_t			panpot_env_sens[SWENV_NUM];

	KeyScaleParam	ksl;
	EnvelopeParam	sw_env[SWENV_NUM];
	LFOParam		sw_lfo[SWLFO_NUM];
	uint8_t			lfo_delay[SWLFO_NUM];
	int8_t			transpose2;		// OPL3で使用
	int8_t			tuning2;		// OPL3で使用
} TonedSynthCommon;

// スロット毎のパッチデータの共通部分
typedef struct SlotPatchCommon {
	uint8_t		velo_sens;
	uint8_t		tl;
	int8_t		lfo_sens[SWLFO_NUM];
	int8_t		env_sens[SWENV_NUM];
	int8_t		ksl_sens;
} SlotPatchCommon;

typedef struct OPMSlotPatch {
	// VolParamsを必ず先頭に配置
	SlotPatchCommon	common;
	
	uint8_t		dt1_mul;
	
	uint8_t		ks_ar;
	uint8_t		ame_d1r;
	uint8_t		dt2_d2r;
	uint8_t		d1l_rr;
} OPMSlotPatch;

typedef struct OPNSlotPatch {
	// VolParamsを必ず先頭に配置
	SlotPatchCommon	common;
	
	uint8_t		dt_mul;

	uint8_t		ks_ar;
	uint8_t		ame_ssgege_dr;
	uint8_t		ssgegn_sr;
	uint8_t		sl_rr;
} OPNSlotPatch;

typedef struct OPMPatch {
	// PatchCommonを先頭に必ず配置
	PatchCommon		common_param;
	TonedSynthCommon	tone_param;
	// その後にSlotPatchを配置
	OPMSlotPatch	slot[4];
	
	uint8_t			fl_con;
	uint8_t			slot_mask;
	uint8_t			ne_nfrq;		///< ne=trueの時はvoice7で発音する
	uint8_t			fastrelease[4];
	uint8_t			reserved[5];
	
	void	loadFromFfopm(const FfopmPatch *p);
	void	writeToFfopm(FfopmPatch *p);
} OPMPatch;

typedef struct OPNPatch {
	// PatchCommonを先頭に必ず配置
	PatchCommon		common_param;
	TonedSynthCommon	tone_param;
	// その後にSlotPatchを配置
	OPNSlotPatch	slot[4];
	
	uint8_t			fb_con;
	uint8_t			fr_slot_mask;		// 4.4 fastRelease slotmask
	
	// ch3専用パラメータ
	uint16_t		ch3_slot_lfo_env_en;	// スロット毎のピッチLFO,EGの有効・無効設定
	
	uint16_t		abs_ch3freq[4];	// 最上位ビットがセットされていれば固定の周波数(8.6固定小数)
	
	void	loadFromFf(const FfPatch *p);
	void	writeToFf(FfPatch *p);

	static const int lfo_env_en_lfo_shift = 0;
	static const int lfo_env_en_eg_shift = 8;
	static const int ch3freq_mask = 0x3fff;
} OPNPatch;

const bool is_carrier_table[8][4]={
	{0,0,0,1},
	{0,0,0,1},
	{0,0,0,1},
	{0,0,0,1},
	{0,0,1,1},
	{0,1,1,1},
	{0,1,1,1},
	{1,1,1,1}
};

void OPMPatch::loadFromFfopm(const FfopmPatch *p)
{
	common_param.patch_type = kPatchTypeOPM_FM;
	common_param.lk_nxp_format_version = USERPATCH_FORMAT_VERSION;

	for (int i=0; i<7; ++i) {
		common_param.name[i] = p->named.patch_name[i];
	}
	common_param.name[7] = 0;

	// X68000を想定して4MHzに設定しておく
	common_param.original_clock = 4000000;
	
	for (int op=0; op<4; ++op) {
		slot[op].dt1_mul = p->named.dt1_mul[op];
		bool is_carrier = is_carrier_table[p->named.fl_con & 0x07][op];
		
		if (is_carrier && flatten_TL_option && p->named.tl[op] == 127) {
			slot[op].common.tl = 0;
		}
		else {
			slot[op].common.tl = p->named.tl[op];
		}
		slot[op].ks_ar = p->named.ks_ar[op];
		slot[op].ame_d1r = p->named.ame_d1r[op];
		slot[op].dt2_d2r = p->named.dt2_d2r[op];
		slot[op].d1l_rr = p->named.d1l_rr[op];
		slot[op].common.velo_sens = is_carrier? 127: 0;
	}
	fl_con = p->named.fl_con;
	slot_mask = 15;
	
	// 未使用パラメータは初期化しておく
	tone_param.transpose = 0;
	tone_param.tuning = 0;
	tone_param.panpot = 0;
	tone_param.panpot_ksl_sens = 0;
	tone_param.pitch_lfo_sens[0] = 0;
	tone_param.pitch_lfo_sens[1] = 0;
	tone_param.pitch_env_sens[0] = 0;
	tone_param.pitch_env_sens[1] = 0;
	tone_param.ksl.min_level = 0;
	tone_param.ksl.max_level = 0;
	tone_param.sw_env[0].inputselect = kModulatorInput_NoInput;
	tone_param.sw_env[1].inputselect = kModulatorInput_NoInput;
	tone_param.sw_lfo[0].midisync_wf_inputselect = kModulatorInput_NoInput;
	tone_param.sw_lfo[1].midisync_wf_inputselect = kModulatorInput_NoInput;
}

void OPMPatch::writeToFfopm(FfopmPatch *p)
{
	for (int op=0; op<4; ++op) {
		p->named.dt1_mul[op] = slot[op].dt1_mul;
		p->named.tl[op] = slot[op].common.tl;
		p->named.ks_ar[op] = slot[op].ks_ar;
		p->named.ame_d1r[op] = slot[op].ame_d1r;
		p->named.dt2_d2r[op] = slot[op].dt2_d2r;
		p->named.d1l_rr[op] = slot[op].d1l_rr;
	}
	p->named.fl_con = fl_con & 0x3f;
	if (tone_param.panpot >= -32) {
		p->named.fl_con |= 0x80;
	}
	if (tone_param.panpot < 32) {
		p->named.fl_con |= 0x40;
	}
	
	for (int i=0; i<7; ++i) {
		p->named.patch_name[i] = common_param.name[i];
	}
}

void OPNPatch::loadFromFf(const FfPatch *p)
{
	common_param.patch_type = kPatchTypeOPN_FM;
	common_param.lk_nxp_format_version = USERPATCH_FORMAT_VERSION;
	
	for (int i=0; i<7; ++i) {
		common_param.name[i] = p->named.patch_name[i];
	}
	common_param.name[7] = 0;
	
	common_param.original_clock = 7987200;
	
	for (int op=0; op<4; ++op) {
		slot[op].dt_mul = p->named.dt_mul[op];
		
		bool is_carrier = is_carrier_table[p->named.fb_con & 0x07][op];
		
		if (is_carrier && flatten_TL_option && p->named.tl[op] == 127) {
			slot[op].common.tl = 0;
		}
		else {
			slot[op].common.tl = p->named.tl[op];
		}
		slot[op].ks_ar = p->named.ks_ar[op];
		slot[op].ame_ssgege_dr = p->named.ame_dr[op];
		slot[op].ssgegn_sr = p->named.sr[op];
		slot[op].sl_rr = p->named.sl_rr[op];
		slot[op].common.velo_sens = is_carrier ? 127: 0;
	}
	fb_con = p->named.fb_con;
	fr_slot_mask = 15;
	
	// 未使用パラメータは初期化しておく
	tone_param.transpose = 0;
	tone_param.tuning = 0;
	tone_param.panpot = 0;
	tone_param.panpot_ksl_sens = 0;
	tone_param.pitch_lfo_sens[0] = 0;
	tone_param.pitch_lfo_sens[1] = 0;
	tone_param.pitch_env_sens[0] = 0;
	tone_param.pitch_env_sens[1] = 0;
	tone_param.ksl.min_level = 0;
	tone_param.ksl.max_level = 0;
	tone_param.sw_env[0].inputselect = kModulatorInput_NoInput;
	tone_param.sw_env[1].inputselect = kModulatorInput_NoInput;
	tone_param.sw_lfo[0].midisync_wf_inputselect = kModulatorInput_NoInput;
	tone_param.sw_lfo[1].midisync_wf_inputselect = kModulatorInput_NoInput;
}

void OPNPatch::writeToFf(FfPatch *p)
{
	for (int op=0; op<4; ++op) {
		p->named.dt_mul[op] = slot[op].dt_mul;
		p->named.tl[op] = slot[op].common.tl;
		p->named.ks_ar[op] = slot[op].ks_ar;
		p->named.ame_dr[op] = slot[op].ame_ssgege_dr;
		p->named.sr[op] = slot[op].ssgegn_sr;
		p->named.sl_rr[op] = slot[op].sl_rr;
	}
	p->named.fb_con = fb_con & 0x3f;
//	if (tone_param.panpot >= -32) {
//		p->named.fb_con |= 0x80;
//	}
//	if (tone_param.panpot < 32) {
//		p->named.fb_con |= 0x40;
//	}
	
	for (int i=0; i<7; ++i) {
		p->named.patch_name[i] = common_param.name[i];
	}
}

int	loadFromOPM(const std::string &in_file_path, OPMPatch *patch, int maxPatches)
{
	int numPatches = 0;
	std::ifstream ifs(in_file_path, std::ios::in);
	
	if (ifs.bad()) {
		std::cout << "can not open file: " << in_file_path << std::endl;
		exit(1);
	}
	
	int patchIndex = 0;
	OPMPatch *p = NULL;
	
	std::string line;
	while (std::getline(ifs, line)) {
		size_t token_pos;
		
		token_pos = line.find("//");
		if (token_pos != std::string::npos) {
			line.erase(token_pos);
		}
		
		token_pos = line.find("@:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 2);
			std::stringstream ss;
			ss << line;
			
			ss >> patchIndex;
			if (patchIndex < maxPatches && patchIndex >= 0) {
				p = &patch[patchIndex];
				
				p->common_param.patch_type = kPatchTypeOPM_FM;
				p->common_param.lk_nxp_format_version = USERPATCH_FORMAT_VERSION;
				// X68000を想定して4MHzに設定しておく
				p->common_param.original_clock = 4000000;
				
				// 未使用パラメータは初期化しておく
				p->tone_param.transpose = 0;
				p->tone_param.tuning = 0;
				p->tone_param.panpot = 0;
				p->tone_param.panpot_ksl_sens = 0;
				p->tone_param.pitch_lfo_sens[0] = 0;
				p->tone_param.pitch_lfo_sens[1] = 0;
				p->tone_param.pitch_env_sens[0] = 0;
				p->tone_param.pitch_env_sens[1] = 0;
				p->tone_param.ksl.min_level = 0;
				p->tone_param.ksl.max_level = 0;
				p->tone_param.sw_env[0].inputselect = kModulatorInput_NoInput;
				p->tone_param.sw_env[1].inputselect = kModulatorInput_NoInput;
				p->tone_param.sw_lfo[0].midisync_wf_inputselect = kModulatorInput_NoInput;
				p->tone_param.sw_lfo[1].midisync_wf_inputselect = kModulatorInput_NoInput;
				
				ss >> std::setw(14) >> p->common_param.name;
				++numPatches;
			}
		}
		
		token_pos = line.find("LFO:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 4);
			std::stringstream ss;
			ss << line;
			
			if (p != NULL) {
				int param;
				ss >> param;
				ss >> param;
				ss >> param;
				ss >> param;
				ss >> param;
				p->ne_nfrq = param & 0x7f;
			}
		}
		
		token_pos = line.find("CH:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			std::stringstream ss;
			ss << line;
			
			if (p != NULL) {
				int param;
				ss >> param;
				p->tone_param.panpot = param - 64;
				ss >> param;
				p->fl_con = (param & 0x07) << 3;
				ss >> param;
				p->fl_con |= param & 0x07;
				ss >> param;
				ss >> param;
				ss >> param;
				p->slot_mask = param >> 3;
				ss >> param;
				p->ne_nfrq = param & 0x80;
			}
		}
		
		int slot = -1;
		token_pos = line.find("M1:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 0;
		}
		token_pos = line.find("M2:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 1;
		}
		token_pos = line.find("C1:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 2;
		}
		token_pos = line.find("C2:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 3;
		}
		
		if (slot != -1) {
			std::stringstream ss;
			ss << line;
			
			OPMSlotPatch *sl = &p->slot[slot];
			if (p != NULL) {
				int param;
				ss >> param;
				sl->ks_ar = param & 0x1f;
				ss >> param;
				sl->ame_d1r = param & 0x1f;
				ss >> param;
				sl->dt2_d2r = param & 0x1f;
				ss >> param;
				sl->d1l_rr = param & 0x0f;
				ss >> param;
				sl->d1l_rr |= (param & 0x0f) << 4;
				ss >> param;
				sl->common.tl = param & 0x7f;
				ss >> param;
				sl->ks_ar |= (param & 0x03) << 6;
				ss >> param;
				sl->dt1_mul = param & 0x0f;
				ss >> param;
				sl->dt1_mul |= (param & 0x07) << 4;
				ss >> param;
				sl->dt2_d2r |= (param & 0x03) << 6;
				ss >> param;
				sl->ame_d1r |= param & 0x80;
			}
		}
	}
	
	return numPatches;
}

bool isValidPatchHeader(const PatchFileHeader &file_header)
{
	if (::strcmp(file_header.sig, sGmcPatchFileSigatute) != 0) {
		return false;
	}
	return true;
}

int	loadFromGtp(const std::string &in_file_path, OPNPatch *patch, int maxPatches)
{
	int numPatches = 0;
	PatchFileHeader	file_header;
	
	std::ifstream ifs(in_file_path, std::ios::in | std::ios::binary);
	
	if (ifs.bad()) {
		std::cout << "can not open file: " << in_file_path << std::endl;
		exit(1);
	}
	
	ifs.seekg(0, std::ios::end);
	uint32_t file_size = static_cast<int>(ifs.tellg());
	ifs.seekg(0, std::ios::beg);
	uint32_t readBytes = patch_file_header_size;

	if (file_size < (patch_file_header_size)) {
		goto bail;
	}
	
	ifs.read((char *)&file_header, patch_file_header_size);
	// ヘッダーが正しいかチェック
	if (isValidPatchHeader(file_header) == false) {
		goto bail;
	}
	
	// １番目のチャンクヘッダの読み込み
	ifs.seekg(file_header.chunk_start_pos, std::ios::beg);
	uint8_t	chunk_header[sizeof(PatchFileChunk)];
	readBytes = sizeof(PatchFileChunk);
	ifs.read((char *)chunk_header, readBytes);
	
	// パッチのタイプ判定
	if (reinterpret_cast<PatchFileChunk *>(chunk_header)->chunk_type == chunk_type_raw_patch) {
		if (reinterpret_cast<PatchFileChunk *>(chunk_header)->chunk_size != PATCHDATA_SIZE) {
			goto bail;
		}
		// データ部分の読み込み
		uint8_t	buffer[PATCHDATA_SIZE];
		readBytes = PATCHDATA_SIZE;
		ifs.read((char *)buffer, PATCHDATA_SIZE);
		// CRCの確認
		if (CalcCrc32(buffer, PATCHDATA_SIZE) != reinterpret_cast<PatchFileChunk *>(chunk_header)->chunk_crc) {
			goto bail;
		}
		
		// 現状0以外のバージョンはエラーとする
		if ((reinterpret_cast<PatchCommon *>(patch)->lk_nxp_format_version & 0x3f) > USERPATCH_FORMAT_VERSION) {
			goto bail;
		}
		
		::memcpy(patch, buffer, PATCHDATA_SIZE);
	}
	else {
		goto bail;
	}
	
	numPatches = 1;
	
bail:
	return numPatches;
}

void getFilePathExtRemoved(const std::string &path, std::string &out, std::string &outExt);
void processInputFile(const std::string &in_file_path);
int processFFFile(const std::string &in_file_path, const std::string &out_file_path);
int processFFOpmFile(const std::string &in_file_path, const std::string &out_file_path);
int processOPMFile(const std::string &in_file_path, const std::string &out_file_path);
int processGtpFile(const std::string &in_file_path, const std::string &out_file_path);
int exportToOPM(const std::string &out_file_path, const OPMPatch *patch, int patchNum);
int exportToGtp(const std::string &out_file_path, const OPNPatch *patch, int patchNum);
int exportToBank(const std::string &out_file_path, const OPNPatch *patch, int patchNum);

void getFilePathExtRemoved(const std::string &path, std::string &out, std::string &outExt)
{
	// 拡張子、パス除去処理
	size_t	len = path.length();
	size_t	extPos = len;
	size_t	bcPos = 0;
	
	extPos = path.find_last_of('.');
	
	bcPos = path.find_last_of(DIRECTORY_SEPARATOR_CHAR) + 1;
	
	if (bcPos > extPos) {
		extPos = bcPos;
	}
	
	out = path.substr(0, extPos);
	outExt = path.substr(extPos + 1);
}

void getFilePathFirectory(const std::string &path, std::string &out)
{
	// ファイル名除去処理
	size_t	bcPos = 0;
	bcPos = path.find_last_of(DIRECTORY_SEPARATOR_CHAR) + 1;
	out = path.substr(0, bcPos);
}


void processInputFile(const std::string &in_file_path)
{
	std::string in_file_name;
	std::string in_file_ext;
	getFilePathExtRemoved(in_file_path, in_file_name, in_file_ext);
	
	std::transform(in_file_ext.begin(), in_file_ext.end(), in_file_ext.begin(), ::tolower);
	
	if (in_file_ext.compare("ff") == 0) {
		std::string out_file_path(in_file_name);
		out_file_path.append(".gtp");
		processFFFile(in_file_path, out_file_path);
	}
	if ((in_file_ext.compare("ffopm") == 0)) {
		std::string out_file_path(in_file_name);
		out_file_path.append(".gtp");
		processFFOpmFile(in_file_path, out_file_path);
	}
//	if (in_file_ext.compare("opm") == 0) {
//		std::string out_file_path(in_file_name);
//		out_file_path.append(".ffopm");
//		processOPMFile(in_file_path, out_file_path);
//	}
	if ((in_file_ext.compare("gtp") == 0)) {
		std::string out_file_path(in_file_name);
		out_file_path.append(".ff");
		processGtpFile(in_file_path, out_file_path);
	}
}

int processFFFile(const std::string &in_file_path, const std::string &out_file_path)
{
	std::cout << in_file_path << std::endl;
	std::cout << "Convert to gtp format." << std::endl;

	int numProcessed = 0;
	std::ifstream ifs(in_file_path, std::ios::in | std::ios::binary);
	
	if (ifs.bad()) {
		std::cout << "can not open file: " << in_file_path << std::endl;
		exit(1);
	}
	
	ifs.seekg(0, std::ios::end);
	int fileSize = static_cast<int>(ifs.tellg());
	ifs.seekg(0, std::ios::beg);
	unsigned char *ffData = new unsigned char[fileSize];
	ifs.read((char *)ffData, fileSize);
	
	
	int	readPtr = 0;
	
	OPNPatch	patch[256];
	int			patchNum = 0;
	
	while (((fileSize - readPtr) >= sizeof(FfPatch)) && (patchNum < 256)) {
		FfPatch	ff;
		::memcpy(ff.raw.data, &ffData[readPtr], sizeof(FfPatch));
		patch[patchNum].loadFromFf(&ff);
		std::cout << patchNum << ": " << patch[patchNum].common_param.name << std::endl;
		readPtr += sizeof(FfPatch);
		++patchNum;
	}
	
	if (patchNum > 1) {
		// 複数のパッチが含まれる場合はバンクファイルを出力
		int banks = (patchNum + 127) / 128;
		for (int bank = 0; bank < banks; ++bank) {
			std::string out_file_dir;
			getFilePathFirectory(out_file_path, out_file_dir);
			out_file_dir.append(sUserPatch[bank]);
			
			exportToBank(out_file_dir, &patch[bank + 128*bank], patchNum > 128?128:patchNum);
			
			patchNum -= 128;
			std::cout << "=>" << out_file_dir << std::endl;
			std::cout << "done." << std::endl;
		}
	}
	else {
		numProcessed = exportToGtp(out_file_path, patch, patchNum);
		std::cout << "=>" << out_file_path << std::endl;
		std::cout << "done." << std::endl;
	}

	return numProcessed;
}

int processFFOpmFile(const std::string &in_file_path, const std::string &out_file_path)
{
	std::cout << in_file_path << std::endl;
	std::cout << "Convert to gtp format." << std::endl;

	int numProcessed = 0;
	std::ifstream ifs(in_file_path, std::ios::in | std::ios::binary);
	
	if (ifs.bad()) {
		std::cout << "can not open file: " << in_file_path << std::endl;
		exit(1);
	}
	
	ifs.seekg(0, std::ios::end);
	int fileSize = static_cast<int>(ifs.tellg());
	ifs.seekg(0, std::ios::beg);
	unsigned char *ffData = new unsigned char[fileSize];
	ifs.read((char *)ffData, fileSize);
	
	
	int	readPtr = 0;
	
	OPMPatch	patch[256];
	int			patchNum = 0;
	
	while (((fileSize - readPtr) >= sizeof(FfopmPatch)) && (patchNum < 256)) {
		FfopmPatch	ffopm;
		::memcpy(ffopm.raw.data, &ffData[readPtr], sizeof(FfopmPatch));
		patch[patchNum].loadFromFfopm(&ffopm);
		std::cout << patchNum << ": " << patch[patchNum].common_param.name << std::endl;
		readPtr += sizeof(FfopmPatch);
		++patchNum;
	}
	
	if (patchNum > 1) {
		// 複数のパッチが含まれる場合はバンクファイルを出力
		int banks = (patchNum + 127) / 128;
		for (int bank = 0; bank < banks; ++bank) {
			std::string out_file_dir;
			getFilePathFirectory(out_file_path, out_file_dir);
			out_file_dir.append(sUserPatch[bank]);
			
			exportToBank(out_file_dir, reinterpret_cast<OPNPatch*>(&patch[bank + 128*bank]), patchNum > 128?128:patchNum);
			
			patchNum -= 128;
			std::cout << "=>" << out_file_dir << std::endl;
			std::cout << "done." << std::endl;
		}
	}
	else {
		numProcessed = exportToGtp(out_file_path, reinterpret_cast<OPNPatch*>(patch), patchNum);
		std::cout << "=>" << out_file_path << std::endl;
		std::cout << "done." << std::endl;
	}

	return numProcessed;
}

int exportToOPM(const std::string &out_file_path, const OPMPatch *patch, int patchNum)
{
	std::ofstream ofs(out_file_path, std::ios::out | std::ios::trunc);
	
	if (!ofs.bad()) {
		ofs << "//MiOPMdrv sound bank Paramer converted by ff2gtp" << std::endl;
		ofs << "//LFO: LFRQ AMD PMD WF NFRQ" << std::endl;
		ofs << "//@:[Num] [Name]" << std::endl;
		ofs << "//CH: PAN	FL CON AMS PMS SLOT NE" << std::endl;
		ofs << "//[OPname]: AR D1R D2R	RR D1L	TL	KS MUL DT1 DT2 AMS-EN" << std::endl;
		
		for (int i=0; i<patchNum; ++i) {
			const OPMPatch *p = &patch[i];
			
			ofs << std::endl;
			ofs << "@:" << i <<  " " << p->common_param.name << std::endl;
			ofs << "LFO:  0   0   0   0"<< std::setw(3) << (p->ne_nfrq & 0x7f) << std::endl;
			ofs << "CH:" << std::setw(3) << (p->tone_param.panpot + 64) << " ";
			ofs << std::setw(3) << ((p->fl_con >> 3) & 0x07) << " ";
			ofs << std::setw(3) << ((p->fl_con) & 0x07) << " ";
			ofs << std::setw(3) << 0 << " ";	// AMS
			ofs << std::setw(3) << 0 << " ";	// PMS
			ofs << std::setw(3) << (p->slot_mask << 3) << " ";
			ofs << std::setw(3) << (p->ne_nfrq & 0x80) << std::endl;
			const int opslot[4] = {0, 2, 1, 3};
			
			const char opName[4][4] = {
				"M1:",
				"C1:",
				"M2:",
				"C2:"
			};
			for (int op=0; op<4; ++op) {
				bool is_carrier = is_carrier_table[((p->fl_con) & 0x07)][opslot[op]];
				const OPMSlotPatch *sl = &p->slot[opslot[op]];
				ofs << opName[op];
				ofs << std::setw(3) << (sl->ks_ar & 0x1f) << " ";
				ofs << std::setw(3) << (sl->ame_d1r & 0x1f) << " ";
				ofs << std::setw(3) << (sl->dt2_d2r & 0x1f) << " ";
				ofs << std::setw(3) << (sl->d1l_rr & 0x0f) << " ";
				ofs << std::setw(3) << (sl->d1l_rr >> 4) << " ";
				if (is_carrier && flatten_TL_option) {
					ofs << "  0 ";
				}
				else {
					ofs << std::setw(3) << (sl->common.tl & 0x7f) << " ";
				}
				ofs << std::setw(3) << (sl->ks_ar >> 6) << " ";
				ofs << std::setw(3) << (sl->dt1_mul & 0x0f) << " ";
				ofs << std::setw(3) << ((sl->dt1_mul >> 4) & 0x07) << " ";
				ofs << std::setw(3) << (sl->dt2_d2r >> 6) << " ";
				ofs << std::setw(3) << (sl->ame_d1r & 0x80) << std::endl;
			}
		}
	}
	else {
		std::cout << "can not open file: " << out_file_path << std::endl;
		return 0;
	}
	
	return patchNum;
}

int exportToGtp(const std::string &out_file_path, const OPNPatch *patch, int patchNum)
{
	std::ofstream ofs(out_file_path, std::ios::out | std::ios::binary | std::ios::trunc);
	
	if (ofs.bad()) {
		std::cout << "can not open file: " << out_file_path << std::endl;
		return 0;
	}
	
	// ヘッダーデータを作成する
	file_header.mb_type_id = 0xffff;
	::strncpy(file_header.mb_serial_no, "999", 8);
	file_header.soundmodule_id = 0;

	uint32_t writeBytes = patch_file_header_size;
	ofs.write(reinterpret_cast<const char*>(&file_header), patch_file_header_size);
	
	// チャンクヘッダーを出力
	PatchFileChunk	chunk_header;
	chunk_header.chunk_type = chunk_type_raw_patch;
	chunk_header.chunk_size = PATCHDATA_SIZE;
	chunk_header.chunk_crc = CalcCrc32(reinterpret_cast<const uint8_t*>(patch), PATCHDATA_SIZE);
	writeBytes = sizeof(PatchFileChunk);
	ofs.write(reinterpret_cast<const char*>(&chunk_header), sizeof(PatchFileChunk));
	
	// パッチデータを出力
	writeBytes = PATCHDATA_SIZE;
	ofs.write(reinterpret_cast<const char*>(patch), PATCHDATA_SIZE);
	
	return patchNum;
}

int exportToBank(const std::string &out_file_path, const OPNPatch *patch, int patchNum)
{
	const FfPatch blankFf = {
		{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'(','b','l','a','n','k',')'}
	};
	OPNPatch blank;
	blank.loadFromFf(&blankFf);
	
	std::ofstream ofs(out_file_path, std::ios::out | std::ios::binary | std::ios::trunc);
	
	if (ofs.bad()) {
		std::cout << "can not open file: " << out_file_path << std::endl;
		return 0;
	}
	
	// ヘッダを出力する
	ofs.write(reinterpret_cast<const char*>(modulemidiMagicData), SIGNATURE_BYTES);
	
	// パッチデータを出力
	for (int i=0; i<128; ++i) {
		if (i < patchNum) {
			ofs.write(reinterpret_cast<const char*>(&patch[i]), PATCHDATA_SIZE);
		}
		else {
			ofs.write(reinterpret_cast<const char*>(&blank), PATCHDATA_SIZE);
		}
	}
	
	return patchNum;
}

int processOPMFile(const std::string &in_file_path, const std::string &out_file_path)
{
	std::cout << in_file_path << std::endl;
	std::cout << "Convert to ffopm format." << std::endl;

	OPMPatch	patch[256];
	int numPatches = loadFromOPM(in_file_path, patch, 256);
	
	std::ofstream ofs(out_file_path, std::ios::out | std::ios::binary | std::ios::trunc);

	for (int i=0; i<numPatches; ++i) {
		FfopmPatch p;
		patch[i].writeToFfopm(&p);
		std::cout << i << ": " << patch[i].common_param.name << std::endl;
		ofs.write(reinterpret_cast<const char*>(p.raw.data), sizeof(FfopmPatch));
	}
	
	std::cout << "=>" << out_file_path << std::endl;
	std::cout << "done." << std::endl;
	
	return 0;
}

int processGtpFile(const std::string &in_file_path, const std::string &out_file_path)
{
	std::cout << in_file_path << std::endl;
	std::cout << "Convert to ff format." << std::endl;

	OPNPatch	patch[128];
	int numPatches = loadFromGtp(in_file_path, patch, 128);
	
	std::ofstream ofs(out_file_path, std::ios::out | std::ios::binary | std::ios::trunc);

	for (int i=0; i<numPatches; ++i) {
		FfPatch p;
		patch[i].writeToFf(&p);
		std::cout << i << ": " << patch[i].common_param.name << std::endl;
		ofs.write(reinterpret_cast<const char*>(p.raw.data), sizeof(FfPatch));
	}
	
	std::cout << "=>" << out_file_path << std::endl;
	std::cout << "done." << std::endl;
	
	return 0;
}

int main(int argc, const char * argv[]) {
	bool help_mode = false;

	std::list<std::string> in_file_list;
	
	// コマンドラインオプションを処理する
	for (int i=1; i<argc; ++i) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
				case 'f':
					// ff=>opm変換時にキャリアのTLを強制的に0に固定する
					flatten_TL_option = true;
					break;
					
				default:
					std::cout << "Invalid option: " << argv[i] << std::endl;
					help_mode = true;
					break;
			}
		}
		else {
			std::string path(argv[i]);
			in_file_list.push_back(path);
		}
	}
	
	if (help_mode || (in_file_list.size() == 0)) {
		std::cout << "Usage: ff2gtp <input> .." << std::endl;
		std::cout << "<input> supports .ff(PMD), .gtp" << std::endl;
		std::cout << "Options:" << std::endl;
//		std::cout << "  -f         Force carrier TL to 0 when converting to .opm" << std::endl;
		exit(0);
	}
	
	std::for_each(in_file_list.begin(), in_file_list.end(), ::processInputFile);
	
	return 0;
}
