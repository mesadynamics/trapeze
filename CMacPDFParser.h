// Trapeze
//
// Copyright (c) 2004 Mesa Dynamics, LLC.  All rights reserved.

#ifndef _H_CMacPDFParser
#define _H_CMacPDFParser
#pragma once

#include "CPDFParser.h"
#include "UPDFMaps.h"

#ifdef __GNUC__
	#include <ApplicationServices/ApplicationServices.h>
#else
	#include <CarbonCore/CarbonCore.h>
	#include <CoreGraphics/CGPDFDictionary.h>
	#include <CoreGraphics/CGPDFDocument.h>
#endif

#if defined(__PowerPlant__)
const ResIDT	PPob_PasswordWindow		= 666;

const PaneIDT	pane_PasswordMessage	= FOUR_CHAR_CODE('PSST'); // LEditText
const PaneIDT	pane_PasswordEntry		= FOUR_CHAR_CODE('PASS'); // LEditText

const PaneIDT	pane_Status				= FOUR_CHAR_CODE('STAT'); // LStaticText
const PaneIDT	pane_Item				= FOUR_CHAR_CODE('ITEM'); // LStaticText
const PaneIDT	pane_Left				= FOUR_CHAR_CODE('LEFT'); // LStaticText
const PaneIDT	pane_Progress			= FOUR_CHAR_CODE('PROG'); // LProgressBar
#else
// March 2013: prepping for free release; replaced fully deprecated FSSpec-based file handling with FSSRef, but
// left instance variable names intact.  Also, code not necessary for the OS X build marked Obsolete10p4. 

class LWindow {
};

class LFile {
public:
	LFile(FSRefPtr inSpec) {
		mMacFileSpec = inSpec;
		mDataForkRefNum = 0;
	}

	virtual ~LFile() {
		try {
			CloseDataFork();
		}

		catch (...) { }
	}

	SInt16 OpenDataFork(SInt8 inPrivileges) {
        HFSUniStr255 dataForkName;
        ::FSGetDataForkName(&dataForkName);
        
		OSErr err = ::FSOpenFork(mMacFileSpec, dataForkName.length, dataForkName.unicode, inPrivileges, &mDataForkRefNum);
		if (err != noErr) {
			mDataForkRefNum = 0;
			throw err;
		}
        
		return mDataForkRefNum;
	}

	void CloseDataFork() {
		if(mDataForkRefNum != 0) {
			OSErr err = ::FSCloseFork(mDataForkRefNum);
			mDataForkRefNum = 0;
			if(err != noErr)
				throw err;
            
            FSCatalogInfo info;
            ::FSGetCatalogInfo(mMacFileSpec, kFSCatInfoVolume, &info, NULL, NULL, NULL);

			::FSFlushVolume(info.volume);
		}
	}	
	void SetSpecifier(FSRefPtr inFileSpec) {
		CloseDataFork();

		mMacFileSpec = inFileSpec;
	}
	
	SInt16 GetDataForkRefNum() const {
		return mDataForkRefNum;
	}
	
private:
	FSRefPtr mMacFileSpec;
	FSIORefNum mDataForkRefNum;
};

typedef unsigned long ArrayIndexT;
#endif

class CMacPDFParser;

class CMacPDFParser : public CPDFParser {
public:
	CMacPDFParser();
	virtual ~CMacPDFParser();
	
	OSErr ConvertToFile(
		FSRefPtr inFile,
		ConstHFSUniStr255Param inFilename = NULL,
		long inType = kWriteASCII,
		long inCount = 1,
		LWindow* inWindow = NULL,
		FSRefPtr outOverrideFile = NULL,
        ConstHFSUniStr255Param outFilename = NULL);

	static bool IsAvailable();

	// options
	void SetEncodeForWord(bool inSet) {
		mEncodeForWord = inSet;
	}
	
	bool GetEncodeForWord() {
		return mEncodeForWord;
	}
	
	void SetShowBreaks(bool inSet) {
		mShowBreaks = inSet;
	}
	
	bool GetShowBreaks() {
		return mShowBreaks;
	}
	
	void SetPromptForPassword(bool inSet) {
		mPromptForPassword = inSet;
	}
	
	bool GetPromptForPassword() {
		return mPromptForPassword;
	}
	
protected:
	// inherited
	virtual OSErr BeginRender(
		size_t inPage,
		float* outWidth,
		float* outHeight);	
		
	virtual void Render();
	
	virtual void EndRender();

protected:
	OSErr StartConversion(
		FSRefPtr inFile);
		
#if defined(CMACPDF_SupportPS)
	OSErr StartPSConversion(
		FSRefPtr inFile);
#endif
	
	void EndConversion();
		
#if defined(CMACPDF_SupportXML)		
	OSErr ConvertToXML(
		bool inUsePropertyListFormat);
#endif
		
	void GetOutputFile(
		FSRefPtr inFile,
		FSRefPtr outFile,
		HFSUniStr255 inExtension);

	// reference counting
	static bool AddObject(
		CGPDFObjectRef inRef,
		ArrayIndexT& outIndex);

	static void ResetObjects();

	//static LArray* GetObjects() {
	//	return sObjects;
	//}

private:
	// text
	static void DictionaryToText(
		const char *key,
		CGPDFObjectRef value,
		void *info);

	static void DictionaryToXObject(
		const char *key,
		CGPDFObjectRef value,
		void *info);

	static void CatalogToFonts(
		const char *key,
		CGPDFObjectRef value,
		void *info);

	static void ExtractFont(
		const char* key,
		CGPDFDictionaryRef dictionary,
		CMacPDFParser* parser);
		
	static wchar_t* ExtractUnicodeMap(
		CGPDFStreamRef stream);

	static char* ExtractCharMap(
		CGPDFStreamRef stream);

	static char* ExtractCharSet(
		CGPDFStringRef stream,
		UPDFMaps::ConstMapParam pdfMap = UPDFMaps::kMacLatinMap);

	static long* ExtractArray(
		char* c,
		long* l);

	static char* FoldMaps(
		wchar_t* unicode,
		char* ansi,
		TextEncoding encoding = kTextEncodingMacRoman);

	static bool UnicodeMapIsValid(
		wchar_t* map);
		
	static bool CharMapIsValid(
		char* map);

#if defined(CMACPDF_SupportXML)		
	// xml
	static void DictionaryToXML(
		const char *key,
		CGPDFObjectRef value,
		void *info);
		
	static void IndexDictionaryToXML(
		const char *key,
		CGPDFObjectRef value,
		void *info,
		size_t index);
		
	// plist	
	static void DictionaryToPLIST(
		const char *key,
		CGPDFObjectRef value,
		void *info);

	static void IndexDictionaryToPLIST(
		const char *key,
		CGPDFObjectRef value,
		void *info,
		size_t index);
#endif
	
protected:
	// document
	CGPDFDocumentRef mPDF;
	CGPDFPageRef mPage;
	
	bool mEncodeForWord;
	bool mShowBreaks;
	bool mPromptForPassword;
	
	LWindow* mWindow;
		
	bool mDeletePDF;	
	bool mDesktopConvert;
	
	FSSpec mPDFSpec;
	
private:
#if defined(__PowerPlant__)
	static LArray* sObjects;
#else
	static std::vector<CGPDFObjectRef> sObjects;
#endif	
    
#if !defined(Obsolete10p4)
    OSErr FSWrite(FSIORefNum refNum, long *count, const void *buffPtr);
#endif
};

#endif
