// Trapeze
//
// Copyright (c) 2004 Mesa Dynamics, LLC.  All rights reserved.

#ifndef _H_CPDFParser
#define _H_CPDFParser
#pragma once

#if defined(WIN32)
	// todo: add .NET text encoding support
#else
	#ifdef __GNUC__
		#include <CoreServices/CoreServices.h>
	#else
		#include <CarbonCore/TextCommon.h>
		#include <CarbonCore/TextEncodingConverter.h>
	#endif
#endif

#include <math.h>
#include <vector>

#define EPS				.01
#define fequal_(x, y)	(-EPS < x - y && x - y < EPS)
#define istoken_(c)		(c == '\\' || c == '{' || c == '}')

const float		kCellWidth			= 6.0;
const float		kCellHeight			= 12.0;
const float		kRTFSpacing			= 1.15;
const float		kRTFWordScriptScale	= 1.50;
const long		kMinScriptLength	= 8;
const long		kMinMidlineSpacing	= 4;
const long		kMaxOperands		= 6;

const long		kMaxQDepth			= 8;

#if defined(WIN32)
typedef UInt32 TextEncoding;

enum {
	kTextEncodingMacRoman = 0,
	kTextEncodingWindowsANSI,
	kTextEncodingUS_ASCII,
	kTextEncodingMacSymbol,
	kTextEncodingMacDingbats,
};
#endif

class CPDFParser {
public:
	enum {
		kNoError = 0,
		kFormatError,
		kVersionError,
		kConvertError,
		kFileReadError,
		kFileWriteError,
		kPDFOpenError,
		kNoPasswordError,
		kNoCopyError,
		kNoPrintError,
		kNoPagesError,
		kNoTextError,
		kBadPageError,
		kMemoryError,
		kUserAbort,
		kPostScriptError,
		kPostScriptFail
	};
	
	enum {
		kWriteASCII = 0,
		kWritePlainText,
		kWriteXML,
		kWritePropertyList,
		kWriteHTML,
		kWriteRTF, // must be penultimate
		kWriteRTFWord // must be ultimate
	};
	
	enum {
		kNewlineDOS = 1,
		kNewlineMac = 2,
		kNewlineUNIX = 3
	};
	
	enum {
		// PDF to Mac Text Encoding constants
		kEncodeMacRoman = kTextEncodingMacRoman,
		kEncodeMacExpert = kTextEncodingMacRoman,
		kEncodeWinANSI = kTextEncodingWindowsANSI,
		
		// custom to Mac Text Encoding constants
		kEncodeASCII = kTextEncodingUS_ASCII,
		kEncodeMacSymbol = kTextEncodingMacSymbol,
		kEncodeMacDingbats = kTextEncodingMacDingbats
	};
	
	enum {
		kTypeFont = 0,
		kTypePage
	};
	
	struct PDFEncoder {
		TextEncoding encoding;

#if defined(WIN32)
		// todo: add .NET text encoding support
#else
		TECObjectRef tec;
#endif
	};
	
	struct PDFXObject {
		char* key;
		unsigned char* data;
		long size;
	};

	struct PDFFontObject {
		long index;

		char* key;
		char* baseFont;
	
		char* map;
		wchar_t* umap;
		
		float* widths;
		
		char* family;
		
		PDFEncoder* encoder;
		
		bool bold;
		bool italic;
		
		unsigned char fi;
		unsigned char fl;
		
		bool mapInPlace;
		//char reserved[30];
	};

	struct PDFTextObject {
		float f;
		float x;
		float y;
		float tx;
		float ty;
		
		unsigned char* text;
		long size;
		long width;
		
		PDFFontObject* font;
		
		char* pre;
		long preSize;
		
		char* post;
		long postSize;	
			
		// post processing	
		long col;
		long line;
		bool ws;
	};
	
	struct StoreObject {
		unsigned char* data;
		long dataSize;
	};

public:
	CPDFParser();
	virtual ~CPDFParser();

	long GetType() {
		return mType;
	}

	void Abort() {
		mAbort = true;
	}

	bool DidAbort() {
		return mAbort;
	}
	
	void Restrict() {
		mRestrict = true;
	}

	bool IsRestricted() {
		return mRestrict;
	}
	
	// options
	void SetPadStripping(bool inSet) {
		mPadStrip = inSet;
	}
	
	bool GetPadStripping() {
		return mPadStrip;
	}
	
	void SetRewrapping(bool inSet) {
		mRewrap = inSet;
	}
		
	bool GetRewrapping() {
		return mRewrap;
	}
		
	void SetRelaxSpacing(bool inSet) {
		mRelaxSpacing = inSet;
	}
	
	bool GetRelaxSpacing() {
		return mRelaxSpacing;
	}
		
	void SetTightSpacing(bool inSet) {
		mTightSpacing = inSet;
	}
	
	bool GetTightSpacing() {
		return mTightSpacing;
	}
		
	void SetSorting(bool inSet) {
		mSort = inSet;
	}
	
	bool GetSorting() {
		return mSort;
	}
	
	void SetNewlineCode(char inSet) {
		mNewlineCode = inSet;
	}
	
	char GetNewlineCode() {
		return mNewlineCode;
	}
		
protected:
	virtual OSErr BeginRender(
		size_t inPage,
		float* outWidth,
		float* outHeight) = 0;	
		
	virtual void Render() = 0;
	
	virtual void EndRender() = 0;
	
protected:
	// rendering
	OSErr RenderPage(
		size_t inPage);

	void ObjectsToRTF();

	void ObjectsToHTML();

	bool Strip();
	
	bool Clean();
	
	bool Rewrap();
	
	void PageToRTF();
	
	void PageToHTML();
	
	void FixNewlines();
	
	void SetPageCount(size_t inCount) {
		mPageCount = inCount;
	}
	
	size_t GetPageCount() {
		return mPageCount;
	}
	
	void SetData(unsigned char* inData, long inDataSize) {
		mData = inData;
		mDataSize = inDataSize;
	}
	
	unsigned char* GetData() {
		return mData;
	}
	
	long GetDataSize() {
		return mDataSize;
	}
		
	void SetEncodingOut(TextEncoding inEncoding) {
		mEncodingOut = inEncoding;
	}
	
	// parsing
	unsigned char* Extract(
		const unsigned char* p,
		long n);
	
	void Store();
	
	void Release();
		
	void Parse(
		const unsigned char* p,
		long n,
		bool init = true);
		
	unsigned long GetParseBytes() {
		return mParseBytes;
	}
		
	// xobject management
	void AddXObject(
		const char* inKey,
		const unsigned char* inData,
		long inSize);	
		
	PDFXObject* GetXObject(
		const char* inKey);
		
	void FreeXObjects();	
		
	// font management
	void AddFont(
		const char* inKey,
		const char* inBaseFont,
		const char* inEncoding,
		char* inMap,
		wchar_t* inUMap,
		float* inWidths,
		unsigned char inFI = 0,
		unsigned char inFL = 0);
	
	PDFFontObject* GetFont(
		const char* inKey);
	
	PDFFontObject* GetFont(
		long inIndex);
		
	long GetFontCount() {
		return mFontTable.size();
	}
			
	void FreeFonts();
	
	// tab management
	void AddTab(
		long inCol);
	
	char* GetTabString();
	
	long GetTabsToCol(
		long inStart,
		long inEnd);
	
	long GetTabIndex(
		long inCol);
	
	long GetTabCount() {
		return mTabTable.size();
	}
	
	void FreeTabs();
	
	// encoder management
	PDFEncoder* AddEncoder(
		TextEncoding inEncoding);
		
	PDFEncoder* GetEncoder(
		TextEncoding inEncoding);
		
	void FreeEncoders();
		
	// page
	long GetTopMargin() {
		return mTopMargin;
	}
		
	long GetLeftMargin() {
		return mLeftMargin;
	}
	
	long GetPageWidth() {
		return mPageWidth;
	}
	
	long GetPageHeight() {
		return mPageHeight;
	}
		
private:
	// rendering
	void Normalize(
		float width,
		float height);
	
	void Fit();
	
	void CalcLines();
	
	long CalcWhitespace();
	
	void CalcExtraTabs();
	
	// parsing
	void InitMetrics();

	void InitChunker();
	
	void OpenChunker(
		long inSize);

	void CloseChunker();

	void ProcessChunk();
		
	void PadChunk(
		long pad);
				
	// converting
	PDFTextObject* AddTextToPage(
		unsigned char* inText = NULL,
		long inSize = 0);

	long MapUnicode(
		wchar_t inCode);

	unsigned char* Map(
		unsigned char* inText,
		long& inSize);
		
	unsigned char* Encode(
		unsigned char* inText,
		long& inSize);
		
	// utils
	float GetCharacterWidth(
		unsigned char c);
		
	float GetTextWidth(
		unsigned char* inText = NULL,
		long inSize = 0);
	
protected:
	long mType;
	size_t mPageCount;
	
	bool mAbort;
	bool mRestrict;
	
	// rendering
	bool mCrop;
	float mCropWidth;
	float mCropHeight;
	
	long mCol;
	long mLine;
	
	bool mPadCols;
	bool mPadLines;
	
	bool mPadStrip;	
	bool mRewrap;
	bool mRelaxSpacing;
	bool mTightSpacing; // 1.4
	bool mSort;
	char mNewlineCode;
	
	bool mFontChanges;
	bool mSizeChanges;
	bool mStyleChanges;
	bool mSuperSubChanges;
	
	TextEncoding mEncodingOut;
		
	// parsing
	unsigned char* mData;
	long mDataSize;
	
	bool mParseIntoStore;
	std::vector<StoreObject*> mStore;

		// persist over page
	std::vector<PDFTextObject*> mPageObjects;
	std::vector<long> mTabTable;
	
		// persist over document
	std::vector<PDFFontObject*> mFontTable;
	std::vector<PDFXObject*> mObjectTable;
	std::vector<PDFEncoder*> mEncoders;

	long mPageWidth;
	long mPageHeight;

	long mPageLength;
	long mPageWeight;
	long mPageWidths;
	long mPageSpacing;
	
	// html page markers
	bool mPageFont;
	bool mPageBold;
	bool mPageItalic;
		
private:
	// rendering
	float ymax;
	float xmax;
	float xs;
	float ys;
	
	// parsing
	bool insideString;
	bool insideHex;
	bool insideArray;
	bool insideText;
	bool insideImage;

	unsigned char* c;
	long cd;

	long mHexStart;
	long mQDepth;
	
	float mSaveScale[kMaxQDepth];
	float mSaveDeltaX[kMaxQDepth];
	float mSaveDeltaY[kMaxQDepth];	
	
	float mScale;
	float mDeltaX;
	float mDeltaY;
	
	float mL;
	float mF;
	float mFS;
	float mX;
	float mY;
	float mLX;
	float mS;
	float mTC;
	float mTW;
	
	float mArrayJ;
	float mArrayX;
	
	float mOperand[kMaxOperands];	

	float mLastL;
	float mLastF;
	float mLastFS;
	float mLastX;
	float mLastY;
	float mTrueY;

	PDFFontObject* mFont;

	bool mAllWhitespace;
	
	unsigned long mParseBytes;
	
	// page info
	long mTopMargin;
	long mLeftMargin;
};

class CTextSorter {
public:
	bool operator() (CPDFParser::PDFTextObject* itemOne, CPDFParser::PDFTextObject* itemTwo) {
		long y1 = lroundf(itemOne->y);
		long y2 = lroundf(itemTwo->y);
		long d = y1 - y2;
		
		if(d >= -1 && d <= 1)
			return itemOne->x < itemTwo->x;

		return y2 < y1;
	}
};

class CTabSorter {
public:
	bool operator() (long itemOne, long itemTwo) {
		return itemOne < itemTwo;
	}
};

#endif
