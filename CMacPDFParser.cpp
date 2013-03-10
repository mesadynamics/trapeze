// Trapeze
//
// Copyright (c) 2004 Mesa Dynamics, LLC.  All rights reserved.

#include "CMacPDFParser.h"

#if defined(__PowerPlant__)
// these are used in PP-based Mesa Dynamics projects
#include "AppConstants.h"
#include "CPreferences.h"
#endif

#if defined(CMACPDF_SupportXML)	
#include "CXMLMacWriter.h"
#include "UXML.h"
#endif

#if defined(CMACPDF_SupportPS)
#include "CDataConsumer.h"
#include "CDataProvider.h"
#include "CPSConverter.h"
#endif

#if defined(CMACPDF_SupportGUI)
#include "UGlobalUtilities.h"
#endif

#include "UPDFMaps.h"

#if defined(__PowerPlant__)
#include <LEditText.h>
#include <LFastArrayIterator.h>
#include <LProgressBar.h>
#include <LStaticText.h>
#endif

#if defined(__PowerPlant__)
LArray* CMacPDFParser::sObjects = NULL;
#else
std::vector<CGPDFObjectRef> CMacPDFParser::sObjects;
#endif

long gProgress = -1;
long gMaxProgress = 0;
char progressStatus[256];

CMacPDFParser::CMacPDFParser() :
		mPDF(NULL),
		mPage(NULL),
		mEncodeForWord(false),
		mShowBreaks(false),
		mPromptForPassword(true),
		mWindow(NULL),
		mDeletePDF(false),
		mDesktopConvert(false)
{
}

CMacPDFParser::~CMacPDFParser()
{
	EndConversion();
}

bool
CMacPDFParser::IsAvailable()
{
#if defined(__PowerPlant__)
	return (UEnvironment::GetOSVersion() >= 0x1030 ? true : false);
#else
	// todo: use gestalt
	return true;
#endif
}

OSErr
CMacPDFParser::ConvertToFile(
	FSRefPtr inFile,
	ConstHFSUniStr255Param inFilename, // NULL in OS X
	long inType,
	long inCount, // 0 in OS X
	LWindow* inWindow, // NULL in OS X
	FSRefPtr outOverrideFile, // non-NULL in OS X
    ConstHFSUniStr255Param outFilename) // non-NULL in OS X
{
	mType = inType;
	mWindow = inWindow;
	
#if defined(__PowerPlant__)
	LProgressBar* bar = NULL;
	LStaticText* status = NULL;
#endif

	OSErr error = StartConversion(inFile);

#if defined(CMACPDF_SupportPS)
	if(error == kPostScriptError)
		error = StartPSConversion(inFile);
#endif

	if(error != kNoError)
		return error;
	
#if defined(Obsolete10p4)
	bool didTruncate = false;
	
	if(inFilename) {
		CFStringRef sr = ::CFStringCreateWithBytes(kCFAllocatorDefault, (unsigned char *) inFilename->unicode, inFilename->length * 2, kCFStringEncodingUnicode, false);
		if(sr) {
			Str255 buffer;
			if(::CFStringGetPascalString(sr, buffer, 256, kCFStringEncodingMacRoman) && buffer[0]) {
				if(buffer[0] > 32) {
					buffer[0] = 32;
					
					while(buffer[0] && buffer[buffer[0]] == ' ')
						buffer[0]--;
						
					didTruncate = true;
				}
				
				//LString::CopyPStr(buffer, inFile->name);
				memcpy(inFile->name, buffer, buffer[0] + 1);
			}
			
			::CFRelease(sr);
		}
	}
#endif

#if defined(CMACPDF_SupportGUI)
	if(mWindow) {
		mWindow->Activate();
		
		status = dynamic_cast<LStaticText*>(mWindow->FindPaneByID(pane_Status));		

		if(inCount) {
			status->SetDescriptor(StringLiteral_("Preprocessing"));
			
			LStr255 itemString("File: ");
			itemString += inFile->name;
			if(didTruncate)
				itemString += 'É';
				
			LStaticText* item = dynamic_cast<LStaticText*>(mWindow->FindPaneByID(pane_Item));		
			item->SetDescriptor(itemString);

			LStr255 leftString("Files remaining: ");
			leftString += (inCount - 1);
			LStaticText* left = dynamic_cast<LStaticText*>(mWindow->FindPaneByID(pane_Left));		
			left->SetDescriptor(leftString);
		}
		else
			status->SetDescriptor(StringLiteral_("Preprocessing PDF"));
		
		bar = dynamic_cast<LProgressBar*>(mWindow->FindPaneByID(pane_Progress));
		bar->SetIndeterminateFlag(true);
		
		if(mWindow->IsVisible() == false) {
			mWindow->Show();
			mWindow->Activate();
			mWindow->UpdatePort();
			
			UGlobalUtilities::NoSpinDelay(40);
		}
	}
#endif		

	gProgress = -1;
				
#ifdef DEBUG
	UInt32 modifiers = ::GetCurrentKeyModifiers();
	if(modifiers & controlKey)
		inType = kWriteXML;
#endif			
			
	FSRefPtr outFile;
	OSType ownerType = 'ttxt';
	
#if defined(CMACPDF_SupportGUI)
	LStr255 typeString;
		
	switch(inType) {
		case kWriteASCII:
			GetOutputFile(inFile, &outFile, StringLiteral_(".asc.txt"));
			SetEncodingOut(kTextEncodingUS_ASCII);
			typeString = "ASCII";
			break;
			
		case kWritePlainText:
			GetOutputFile(inFile, &outFile, StringLiteral_(".txt"));
			SetEncodingOut(kTextEncodingMacRoman);
			typeString = "plain text";
			break;
			
		case kWriteXML:
			GetOutputFile(inFile, &outFile, StringLiteral_(".xml"));
			typeString = "XML";
			break;
			
		case kWritePropertyList:
			GetOutputFile(inFile, &outFile, StringLiteral_(".plist"));
			ownerType = 'pled';
			typeString = "Property List";
			break;
			
		case kWriteHTML:
			GetOutputFile(inFile, &outFile, StringLiteral_(".html"));
			SetEncodingOut(kCFStringEncodingWindowsLatin1);
			ownerType = 'sfri';
			typeString = "HTML";
			break;
			
		case kWriteRTF:
			GetOutputFile(inFile, &outFile, StringLiteral_(".rtf"));
			SetEncodingOut(kTextEncodingMacRoman);
			typeString = "RTF (TextEdit)";
			break;
			
		case kWriteRTFWord:
			GetOutputFile(inFile, &outFile, StringLiteral_(".doc"));
			SetEncodingOut(kTextEncodingMacRoman);
			ownerType = 'WORD';
			typeString = "RTF (Word)";
			break;	
	}		
#else
	switch(inType) {
		case kWriteASCII:
			SetEncodingOut(kTextEncodingUS_ASCII);
			break;
			
		case kWritePlainText:
			SetEncodingOut(kTextEncodingMacRoman);
			break;
						
		case kWritePropertyList:
			ownerType = 'pled';
			break;
			
		case kWriteHTML:
			SetEncodingOut(kCFStringEncodingWindowsLatin1);
			ownerType = 'sfri';
			break;
			
		case kWriteRTF:
			SetEncodingOut(kTextEncodingMacRoman);
			break;
			
		case kWriteRTFWord:
			SetEncodingOut(kTextEncodingMacRoman);
			ownerType = 'WORD';
			break;	
	}		
#endif

	if(outOverrideFile)
		outFile = outOverrideFile;

#if defined(CMACPDF_SupportRenaming)
	FSSpec fileSpec;
	OSErr err = ::FSMakeFSSpec(outFile.vRefNum, outFile.parID, outFile.name, &fileSpec);

	Str255 saveName;
	LString::CopyPStr(outFile.name, saveName);
	
	while(err == bdNamErr) {
		Str255 error = StringLiteral_("");
		
		if(UGlobalUtilities::AskForOneString(LCommander::GetTopCommander(), PPob_AskNameWindow, pane_Name, saveName, error) == false)
			return kFileWriteError;
			
		err = ::FSMakeFSSpec(outFile.vRefNum, outFile.parID, saveName, &outFile);
	}
#endif
				
	LFile* output = new LFile(outFile);
	if(output) {
		try {
            FSRef newOutFile;
            OSErr err = ::FSCreateFileUnicode(outFile, outFilename->length, outFilename->unicode, kFSCatInfoNone, NULL, &newOutFile, NULL);
            
#if defined(Obsolete10p4)
			OSErr err = ::FSpCreate(&outFile, ownerType,  (inType >= kWriteRTF ? 'RTF ' : 'TEXT'), smSystemScript);
			
			//output->CreateNewDataFile(ownerType, (inType >= kWriteRTF ? 'RTF ' : 'TEXT'));
			
			if(outOverrideFile == false) { // 1.3
				if(err == wrPermErr || err == kWriteProtectedErr || err == afpAccessDenied) {			
					SInt16	theVRef;
					SInt32	theDirID;

					if(::FindFolder(kOnSystemDisk, kDesktopFolderType, true, &theVRef, &theDirID) == noErr) {
						outFile.vRefNum = theVRef;
						outFile.parID = theDirID;
						
						output->SetSpecifier(outFile);
						
						err = ::FSpCreate(&outFile, ownerType,  (inType >= kWriteRTF ? 'RTF ' : 'TEXT'), smSystemScript);
						
						mDesktopConvert = true;
					}	
				}
			}
#else
            if(err == noErr)
                output->SetSpecifier(&newOutFile);
#endif
            
#if defined(__PowerPlant__)
			ThrowIfOSErr_(err);
#else
			if(err != noErr)
				throw err;
#endif
            			
			if(inType == kWritePropertyList || inType == kWriteXML) {
#if defined(CMACPDF_SupportXML)	
				CXMLTree::Open();
	
				ConvertToXML((inType == kWritePropertyList ? true : false));
	
				CXMLMacFileWriter* outputXML = new CXMLMacFileWriter(output);
				if(outputXML) {
					CXMLWriter::SetASCIIFormat(CXMLWriter::asciiUNIX);
					
					CXMLNode* comment = new CXMLNode("xml");
				 	comment->SetAttribute("version", "1.0");
					if(inType == kWritePropertyList)
				 		comment->SetAttribute("encoding", "UTF-8");
					else
				 		comment->SetAttribute("encoding", "ISO-8859-1");
				 	comment->SetType(CXMLNode::node_Instruction);
					comment->Write(outputXML);
					delete comment;				
						
					if(inType == kWritePropertyList) {
						char doctype[256] = "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">";
						outputXML->WriteBytes(doctype, strlen(doctype));
						outputXML->Newline(false);
					}
							
					CXMLTree::PopAll();
					CXMLTree::GetCurrentNode()->Write(outputXML);
					
					outputXML->Newline(false);
					
					delete outputXML;
				}

				CXMLTree::Close();	
#endif
			}
			else {
				long dataBytes = 0;
				
				output->OpenDataFork(fsRdWrPerm);
				
				// extract all the fonts in the catalog to build our font table
				CGPDFDictionaryRef catalog = ::CGPDFDocumentGetCatalog(mPDF);
				if(catalog)
					::CGPDFDictionaryApplyFunction(catalog, CatalogToFonts, this);
				
				ResetObjects();
				
				long fontCount = GetFontCount();

				// render the document, one page at a time
				if(inType >= kWriteRTF) {
					char header[256] = "{\\rtf1\\mac\\ansicpg10000\n";
					
					if(GetPadStripping() == false)
						strcat(header, "\\viewkind1\\viewscale100\\viewzk2\n");
					
					if(fontCount)
						strcat(header, "{\\fonttbl");

					long bytes = strlen(header);
					FSWrite(output->GetDataForkRefNum(), &bytes, header);
					
					if(fontCount) {
						for(long i = 0; i < fontCount; i++) {
							PDFFontObject* f = GetFont(i);
							
							char fontEntry[256];
							sprintf(fontEntry, "\\f%d\\f%s\\fcharset77 %s;", (int)i, (f->family == NULL ? "nil" : f->family), f->baseFont);
															
							long bytes = strlen(fontEntry);
							FSWrite(output->GetDataForkRefNum(), &bytes, fontEntry);
						}
						
						{
							char fontEntry[256];
							sprintf(fontEntry, "\\f%d\\fmodern\\fcharset77 Courier;}", (int)fontCount);
							
							long bytes = strlen(fontEntry);
							FSWrite(output->GetDataForkRefNum(), &bytes, fontEntry);
						}
					}
				}
				else if(inType == kWriteHTML) {
                    CFStringRef filename;
                    ::LSCopyDisplayNameForRef(inFile, &filename);

                    CFIndex buf_len = CFStringGetMaximumSizeForEncoding ( CFStringGetLength( filename ), kCFStringEncodingUTF8 ) + 1;
                    
                    char* title = NULL;
                    char buffer[buf_len];
                    
                    Boolean success = CFStringGetCString(filename,buffer,buf_len,kCFStringEncodingUTF8);
                    if(success)
                        title = &(buffer[0]);
                    else
                        title = (char*)"Untitled";

					char header[256];
					if(mNewlineCode == kNewlineMac)
						sprintf(header, "<html>\r<head>\r\t<meta http-equiv=\"content-type\" content=\"text/html; charset=iso-8859-1\">\r\t<meta name=\"GENERATOR\" content=\"Trapeze\">\r\t<title>%s</title>\r</head>\r", title);
					else if(mNewlineCode == kNewlineDOS)
						sprintf(header, "<html>\r\n<head>\r\n\t<meta http-equiv=\"content-type\" content=\"text/html; charset=iso-8859-1\">\r\n\t<meta name=\"GENERATOR\" content=\"Trapeze\">\r\n\t<title>%s</title>\r\n</head>\r\n", title);
					else
						sprintf(header, "<html>\n<head>\n\t<meta http-equiv=\"content-type\" content=\"text/html; charset=iso-8859-1\">\n\t<meta name=\"GENERATOR\" content=\"Trapeze\">\n\t<title>%s</title>\n</head>\n", title);
					
					long bytes = strlen(header);
					FSWrite(output->GetDataForkRefNum(), &bytes, header);
                    
                    if(filename)
                        CFRelease(filename);
				}
				
				size_t pages = GetPageCount();
				size_t savePages = pages;
				
#if defined(CMACPDF_SupportGUI)
				if(mWindow && DidAbort() == false) {
					LStr255 statusString;
					if(inCount) {
						statusString = "Converting ";
						statusString += (SInt32) pages;
						if(pages > 1)
							statusString += " pages to ";
						else
							statusString += " page to ";
						statusString += typeString;
						
						if(mDesktopConvert)
							statusString += " on Desktop";
					}
					else {
						statusString = "Reading PDF (";
						statusString += (SInt32) pages;
						if(pages > 1)
							statusString += " pages)";
						else
							statusString += " page)";
					}
										
					status->SetDescriptor(statusString);
					
					bar->SetIndeterminateFlag(false);
					bar->SetValue(0);
					bar->SetMaxValue(pages);

					mWindow->UpdatePort();
					
					UGlobalUtilities::NoSpinDelay(40);
				}
#endif

				gProgress = 0;
				gMaxProgress = pages;
				
				for(size_t i = 1; i <= pages; i++) {
					if(DidAbort())
						break;
						
					if(IsRestricted() && i >= 3)
						pages = i;			
					
					RenderPage(i);
					
#if defined(CMACPDF_SupportGUI)
					if(bar)
						bar->SetValue(i);
#endif

					gProgress = i;
						
					if(inType >= kWriteRTF) {
						char pageStart[256];
						sprintf(pageStart, "\\plain\\pard\\li0\\sl%d\n", (int)lroundf(12.0 * 20.0 * kRTFSpacing));
						
						long bytes = strlen(pageStart);
						FSWrite(output->GetDataForkRefNum(), &bytes, pageStart);
					}
					else if(inType == kWriteHTML) {
						char pageStart[256];
						if(mNewlineCode == kNewlineMac)
							sprintf(pageStart, "\r<a name=\"page_%d\"></a>\r", (int)i);
						else if(mNewlineCode == kNewlineDOS)
							sprintf(pageStart, "\r\n<a name=\"page_%d\"></a>\r\n", (int)i);
						else
							sprintf(pageStart, "\n<a name=\"page_%d\"></a>\n", (int)i);
							
						long bytes = strlen(pageStart);
						FSWrite(output->GetDataForkRefNum(), &bytes, pageStart);
					}
								
					if(inType == kWriteRTFWord) {
						if(GetPadStripping() == false) {
							long leftMargin = GetLeftMargin();
							if(leftMargin) {
								char marginEntry[256];
								sprintf(marginEntry, "\\marglsxn%d\\margrsxn%d\n", 20 * (int)leftMargin, 0 * (int)leftMargin);
									
								long bytes = strlen(marginEntry);
								FSWrite(output->GetDataForkRefNum(), &bytes, marginEntry);
							}
							
							long topMargin = GetTopMargin();
							if(topMargin) {
								char marginEntry[256];
								sprintf(marginEntry, "\\margtsxn%d\\margbsxn%d\n", 20 * (int)topMargin, 0 * (int)topMargin);
									
								long bytes = strlen(marginEntry);
								FSWrite(output->GetDataForkRefNum(), &bytes, marginEntry);
							}
						}
						else {
							char marginEntry[256] = "\\margl0\\margr0\\margt0\\margb0\n";
							long bytes = strlen(marginEntry);
							FSWrite(output->GetDataForkRefNum(), &bytes, marginEntry);
						}
						
						/* A4 Paper Size
						{
							long pageWidth = GetPageWidth();
							long pageHeight = GetPageHeight();
							if(pageWidth && pageHeight) {
								char pageEntry[256];
								sprintf(pageEntry, "\\pgwsxn%d\\pghsxn%d\n", 11899, 16838);
									
								long bytes = strlen(pageEntry);
								FSWrite(output->GetDataForkRefNum(), &bytes, pageEntry);
							}
						}*/
						
						long pageWidth = GetPageWidth();
						long pageHeight = GetPageHeight();
						if(pageWidth && pageHeight) {
							char pageEntry[256];
							sprintf(pageEntry, "\\pgwsxn%d\\pghsxn%d\n", 20 * (int)pageWidth, 20 * (int)pageHeight);
								
							long bytes = strlen(pageEntry);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEntry);
						}
					}
					else if(inType == kWriteRTF && GetPadStripping() == false) {
						char marginEntry[256] = "\\margl0\\margr0\\margt0\\margb0\n";
						long bytes = strlen(marginEntry);
						FSWrite(output->GetDataForkRefNum(), &bytes, marginEntry);
					}
					else if(inType == kWriteHTML) {
						if(GetPadStripping() == false) {
							long leftMargin = GetLeftMargin();
							if(leftMargin) {
								char marginEntry[256];
								if(mNewlineCode == kNewlineMac)
									sprintf(marginEntry, "\r<blockquote width=%d>\r", (int)leftMargin);
								else if(mNewlineCode == kNewlineDOS)
									sprintf(marginEntry, "\r\n<blockquote width=%d\r\n", (int)leftMargin);
								else
									sprintf(marginEntry, "\n<blockquote width=%d>\n", (int)leftMargin);
									
								long bytes = strlen(marginEntry);
								FSWrite(output->GetDataForkRefNum(), &bytes, marginEntry);
							}
						}
					}

					if(inType >= kWriteRTF) {
						char* tabs = GetTabString();
						
						if(tabs) {
							char tabEntry[256];
							sprintf(tabEntry, "%s\n", tabs);
							
							long bytes = strlen(tabEntry);
							FSWrite(output->GetDataForkRefNum(), &bytes, tabEntry);
							
							free(tabs);
						}
					}
					else if(i == pages && GetPadStripping() == false) {
						// todo: should probably take newline encoding into account here
						
						while(mDataSize > 1 && mData[mDataSize - 1] == '\n' && mData[mDataSize - 2] == '\n')
							mDataSize--;
					}
															
					if(mData) {
						dataBytes += mDataSize;
						FSWrite(output->GetDataForkRefNum(), &mDataSize, mData);
				
						free(mData);
						mData = NULL;
						
						mDataSize = 0;
					}

					if(inType == kWriteHTML) {
						if(GetPadStripping() == false) {
							long leftMargin = GetLeftMargin();
							if(leftMargin) {
								char marginEntry[256];
								if(mNewlineCode == kNewlineMac)
									sprintf(marginEntry, "\r</blockquote>\r");		
								else if(mNewlineCode == kNewlineDOS)
									sprintf(marginEntry, "\r\n</blockquote>\r\n");		
								else
									sprintf(marginEntry, "\n</blockquote>\n");		
												
								long bytes = strlen(marginEntry);
								FSWrite(output->GetDataForkRefNum(), &bytes, marginEntry);
							}
						}
					}

					if(i < pages) {
						if(inType == kWriteRTFWord && GetPadStripping() == false) {
							char pageEnd[256] = "\\sect\n";
							long bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
						}
						else if(inType == kWriteRTF && GetPadStripping() == false) {
							char pageEnd[256] = "\\page\n";
							long bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
						}
						else if(mShowBreaks) {
							if(inType >= kWriteRTF) {
								char pageEnd[256];
								sprintf(pageEnd, "\\plain\\li0\\f%d ------------------------------[PAGE BREAK]------------------------------\\\n", (int)fontCount);
								long bytes = strlen(pageEnd);
								FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							}
							else {
								if(mType == kWriteHTML) {
									char tag[16];
									if(mNewlineCode == kNewlineMac)
										sprintf(tag, "\r<pre>\r");
									else if(mNewlineCode == kNewlineDOS)
										sprintf(tag, "\r\n<pre>\r\n");
									else
										sprintf(tag, "\n<pre>\n");
										
									long bytes = strlen(tag);
									FSWrite(output->GetDataForkRefNum(), &bytes, tag);
								}
							
								char pageEnd[256];
								if(mNewlineCode == kNewlineMac)
									sprintf(pageEnd, "------------------------------[PAGE BREAK]------------------------------\r");
								else if(mNewlineCode == kNewlineDOS)
									sprintf(pageEnd, "------------------------------[PAGE BREAK]------------------------------\r\n");
								else
									sprintf(pageEnd, "------------------------------[PAGE BREAK]------------------------------\n");
								
								long bytes = strlen(pageEnd);
								FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							}

							if(mType == kWriteHTML) {
								char tag[16];
								if(mNewlineCode == kNewlineMac)
									sprintf(tag, "</pre>\r");
								else if(mNewlineCode == kNewlineDOS)
									sprintf(tag, "</pre>\r\n");
								else
									sprintf(tag, "</pre>\n");
										
								long bytes = strlen(tag);
								FSWrite(output->GetDataForkRefNum(), &bytes, tag);
							}
							
						}
					}
					else if(IsRestricted()) {
						if(inType >= kWriteRTF) {
							char pageEnd[256];
							sprintf(pageEnd, "\\plain\\li0\\f%d \\\n=====================================\\\n", (int)fontCount);
							long bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							
							if(savePages > 3) {
								sprintf(pageEnd, "\\plain\\f%d  Conversion limited to three pages.\\\n", (int)fontCount);
								bytes = strlen(pageEnd);
								FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							}
														
							sprintf(pageEnd, "\\plain\\f%d    Thank you for trying Trapeze!\\\n", (int)fontCount);
							bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							
							sprintf(pageEnd, "\\plain\\f%d =====================================\\\n", (int)fontCount);
							bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
						}
						else {
							if(mType == kWriteHTML) {
								char tag[16];
								if(mNewlineCode == kNewlineMac)
									sprintf(tag, "\r<pre>\r");
								else if(mNewlineCode == kNewlineDOS)
									sprintf(tag, "\r\n<pre>\r\n");
								else
									sprintf(tag, "\n<pre>\n");
										
								long bytes = strlen(tag);
								FSWrite(output->GetDataForkRefNum(), &bytes, tag);
							}
							
							char pageEnd[256];
							if(mNewlineCode == kNewlineMac)
								sprintf(pageEnd, "\r=====================================\r");
							else if(mNewlineCode == kNewlineDOS)
								sprintf(pageEnd, "\r\n=====================================\r\n");
							else
								sprintf(pageEnd, "\n=====================================\n");
							
							long bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							
							if(savePages > 3) {
								if(mNewlineCode == kNewlineMac)
									sprintf(pageEnd, " Conversion limited to three pages.\r");	
								else if(mNewlineCode == kNewlineDOS)
									sprintf(pageEnd, " Conversion limited to three pages.\r\n");	
								else
									sprintf(pageEnd, " Conversion limited to three pages.\n");	
	
								bytes = strlen(pageEnd);
								FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							}
															
							if(mNewlineCode == kNewlineMac)
								sprintf(pageEnd, "   Thank you for trying Trapeze!\r");
							else if(mNewlineCode == kNewlineDOS)
								sprintf(pageEnd, "   Thank you for trying Trapeze!\r\n");
							else
								sprintf(pageEnd, "   Thank you for trying Trapeze!\n");
							
							bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);
							
							if(mNewlineCode == kNewlineMac)
								sprintf(pageEnd, "=====================================\r");
							else if(mNewlineCode == kNewlineDOS)
								sprintf(pageEnd, "=====================================\r\n");
							else
								sprintf(pageEnd, "=====================================\n");
							
							bytes = strlen(pageEnd);
							FSWrite(output->GetDataForkRefNum(), &bytes, pageEnd);

							if(mType == kWriteHTML) {
								char tag[16];
								if(mNewlineCode == kNewlineMac)
									sprintf(tag, "</pre>\r");
								else if(mNewlineCode == kNewlineDOS)
									sprintf(tag, "</pre>\r\n");
								else
									sprintf(tag, "</pre>\n");

								long bytes = strlen(tag);
								FSWrite(output->GetDataForkRefNum(), &bytes, tag);
							}
						}
						
#if defined(CMACPDF_SupportGUI)
						if(savePages > 3)
							UGlobalUtilities::NoSpinDelay(60);
#endif
					}
	
					// done with tab table
					FreeTabs();
				}
				
				if(inType >= kWriteRTF) {
					char footer[16] = "\n}\n";
					long bytes = strlen(footer);
					FSWrite(output->GetDataForkRefNum(), &bytes, footer);
				}
				else if(inType == kWriteHTML) {
					char footer[16];
					if(mNewlineCode == kNewlineMac)
						sprintf(footer, "</html>\r");
					else if(mNewlineCode == kNewlineDOS)
						sprintf(footer, "</html>\r\n");
					else
						sprintf(footer, "</html>\n");

					long bytes = strlen(footer);
					FSWrite(output->GetDataForkRefNum(), &bytes, footer);
				}
				
				// done with font table
				FreeFonts();
				
				// done with xobjects
				FreeXObjects();
				
				// dont with encoders
				FreeEncoders();
				
				if(mAbort || dataBytes == 0) {
					output->CloseDataFork();
					::FSDeleteObject(outFile);
					
					if(mAbort)
						error = kUserAbort;
					else
						error = kNoTextError;
				}
			}
		}
		
		catch (...) {
			error = kFileWriteError;
		}
		
		delete output;
	}
		
	EndConversion();
	
#if defined(CMACPDF_SupportGUI)
	if(mWindow) {
		bar->SetValue(bar->GetMaxValue());

		mWindow->UpdatePort();
		
		UGlobalUtilities::NoSpinDelay(40);	
	}
#endif

	gProgress = gMaxProgress;

	return error;
}

#pragma mark -

OSErr
CMacPDFParser::BeginRender(
	size_t inPage,
	float* outWidth,
	float* outHeight)
{
	mPage = ::CGPDFDocumentGetPage(mPDF, inPage);	
	if(mPage == NULL)
		return kBadPageError;
		
	::CGPDFPageRetain(mPage);
	
	CGRect mediaBox = ::CGPDFPageGetBoxRect(mPage, kCGPDFMediaBox);

	CGRect cropBox = ::CGPDFPageGetBoxRect(mPage, kCGPDFCropBox);
	
	if(cropBox.size.width && cropBox.size.height && cropBox.size.width < mediaBox.size.width && cropBox.size.height < mediaBox.size.height) {
		*outWidth = cropBox.size.width;
		*outHeight = cropBox.size.height;
		
		mCropWidth = cropBox.origin.x;
		mCropHeight = cropBox.origin.y;
		mCrop = true;
	}
	else {	
		*outWidth = mediaBox.size.width;
		*outHeight = mediaBox.size.height;
	}
		
	return kNoError;
}
	
void
CMacPDFParser::Render()
{
	if(mPage) {
		CGPDFDictionaryRef dictionary = ::CGPDFPageGetDictionary(mPage);
		if(dictionary)
			::CGPDFDictionaryApplyFunction(dictionary, DictionaryToText, this);
	}
}

void
CMacPDFParser::EndRender()
{
	if(mPage) {					
		::CGPDFPageRelease(mPage);
		mPage = NULL;
	}		
}

#pragma mark -

#if defined(CMACPDF_SupportPS)

OSErr
CMacPDFParser::StartPSConversion(
	FSSpecPtr inFile)
{
	bool success = false;
	
	CDataConsumer* pdfConsumer = NULL;
	
	CGDataProviderRef ps = NULL;
	CGDataConsumerRef pdf = NULL;
	
	// we need a temp pdf file
	{
		SInt16 theVRef;
		SInt32 theDirID;

		if(::FindFolder(kOnSystemDisk, kTemporaryFolderType, true, &theVRef, &theDirID) != noErr) {
			if(::FindFolder(kUserDomain, kTemporaryFolderType, true, &theVRef, &theDirID) != noErr)
				return kPostScriptFail;
		}
			
		LStr255 temp("trap_");
		temp += (SInt32) TickCount();
		temp += ".pdf";	
			
		::FSMakeFSSpec(theVRef, theDirID, temp, &mPDFSpec);
		::FSpDelete(&mPDFSpec);

		if(::FSpCreate(&mPDFSpec, sig_Appplication, 'PDF ', smSystemScript) != noErr)
			return kPostScriptFail;	
	}

	// use the temp file as the output destination
	pdfConsumer = new CDataConsumer(&mPDFSpec);
	if(pdfConsumer == NULL)
		goto abort;
		
	pdf = pdfConsumer->GetConsumer();

	if(pdf == NULL) 
		goto abort;
		
	::CGDataConsumerRetain(pdf);

	// use the input file as the conversion source
	ps = CDataProvider::ProviderFromFSSpec(inFile);
		
	if(ps == NULL)
		goto abort;
			
	::CGDataProviderRetain(ps);
	
	CPSConverter* converter = new CPSConverter;
	if(converter) {
		success = ::CGPSConverterConvert(converter->GetConverter(), ps, pdf, NULL);
		delete converter;
	}
		
abort:	
	if(ps)
		::CGDataProviderRelease(ps);
		
	if(pdf)
		::CGDataConsumerRelease(pdf);
		
	if(pdfConsumer)
		delete pdfConsumer;
		
	if(success) {
		mDeletePDF = true;
		return StartConversion(&mPDFSpec);
	}
	
	::FSpDelete(&mPDFSpec);
	
	return kPostScriptFail;
}

#endif // CMACPDF_SupportPS

OSErr
CMacPDFParser::StartConversion(
	FSRefPtr inFile)
{
    LFile* input = new LFile(inFile);
        
    try {
       if(input->OpenDataFork(fsRdPerm)) {
            long header = 0;
            long version = 0;
            SInt32 swap;
            
            long bytes = sizeof(SInt32);
            ::FSReadFork(input->GetDataForkRefNum(), fsAtMark, 0, bytes, &swap, NULL);
            header = CFSwapInt32BigToHost(swap);

            bytes = sizeof(SInt32);
            ::FSReadFork(input->GetDataForkRefNum(), fsAtMark, 0, bytes, &swap, NULL);
            version = CFSwapInt32BigToHost(swap);

            input->CloseDataFork();
           
            if(header == '%!PS')
                return kPostScriptError;
            
            if(header != '%PDF')
                return kFormatError;

#if defined(__PowerPlant__)
            if(UEnvironment::GetOSVersion() < 0x1040) {
                if(version >= '-1.6')
                    return kVersionError;
            }	
            else {
                if(version >= '-1.7')
                    return kVersionError;
            }	
#else
            // use Gestalt
#endif
                
        }
       else {
           delete input;
           return kFileReadError;
       }
    }
  
    catch (...) {
        delete input;
        return kFileReadError;
    }

	CFURLRef url = ::CFURLCreateFromFSRef(kCFAllocatorDefault, inFile);
	if(url == NULL)
		return kFileReadError;
		
	mPDF = ::CGPDFDocumentCreateWithURL(url);
	
	::CFRelease(url);	

	if(mPDF == NULL)
		return kPDFOpenError;
		
	::CGPDFDocumentRetain(mPDF);
			
	bool askForPassword = false;
	
	if(::CGPDFDocumentIsUnlocked(mPDF) == false && ::CGPDFDocumentIsEncrypted(mPDF))
		askForPassword = true;
	/*else if(::CGPDFDocumentAllowsCopying(mPDF) == false)
		askForPassword = true;
	else if(::CGPDFDocumentAllowsPrinting(mPDF) == false)
		askForPassword = true;*/
			
	if(askForPassword && mPromptForPassword == false) {
		::CGPDFDocumentRelease(mPDF);
		mPDF = NULL;

		return kNoPasswordError;
	}		
			
#if defined(CMACPDF_SupportGUI)
	if(askForPassword) {
		do {
			{
				StDialogHandler pass(PPob_PasswordWindow, LCommander::GetTopCommander());
				LWindow* dialog = pass.GetDialog();
				
				LStr255 messageString("The file Ò");
				messageString += inFile->name;
				messageString += "Ó is password protected.  Please enter a password to unlock it:";
				
				LStaticText* message = dynamic_cast<LStaticText*>(dialog->FindPaneByID(pane_PasswordMessage));
				message->SetDescriptor(messageString);

				LEditText* password = dynamic_cast<LEditText*>(dialog->FindPaneByID(pane_PasswordEntry));
				dialog->SetLatentSub(password);
				dialog->SwitchTarget(password);
				
				dialog->Show();
				
				MessageT msg = msg_Nothing;
				do {
					msg = pass.DoDialog();
				} while(msg != msg_OK && msg != msg_Cancel);
				
				if(msg == msg_OK) {
					Str255 passwordString;
					password->GetDescriptor(passwordString);
					
					char passwordCString[256];
					::CopyPascalStringToC(passwordString, passwordCString);
					
					::CGPDFDocumentUnlockWithPassword(mPDF, passwordCString);
				}
				else if(msg == msg_Cancel) {
					::CGPDFDocumentRelease(mPDF);
					mPDF = NULL;
					
					return kNoPasswordError;
				}
			}
		} while(::CGPDFDocumentIsUnlocked(mPDF) == false);	
	}
#else
	// todo: ask for password via sscanf
#endif
	
	/*if(::CGPDFDocumentAllowsCopying(mPDF) == false) {
		::CGPDFDocumentRelease(mPDF);
		mPDF = NULL;
		
		return kNoCopyError;
	}
	
	if(::CGPDFDocumentAllowsPrinting(mPDF) == false) {
		::CGPDFDocumentRelease(mPDF);
		mPDF = NULL;
		
		return kNoPrintError;
	}*/
	
	size_t pageCount = ::CGPDFDocumentGetNumberOfPages(mPDF);
	if(pageCount == 0) {
		::CGPDFDocumentRelease(mPDF);
		mPDF = NULL;
		
		return kNoPagesError;
	}
	
	SetPageCount(pageCount);
		
#if defined(__PowerPlant__)
	sObjects = new LArray;
#endif
	
	return kNoError;
}

void
CMacPDFParser::EndConversion()
{
	if(mData) {
		free(mData);
		mData = NULL;
	}
	
	mDataSize = 0;
	
#if defined(__PowerPlant__)
	if(sObjects) {
		delete sObjects;
		sObjects = NULL;
	}
#else
	sObjects.clear();
#endif	
	
	if(mPage) {					
		::CGPDFPageRelease(mPage);
		mPage = NULL;
	}		

	if(mPDF) {
		::CGPDFDocumentRelease(mPDF);
		mPDF = NULL;
	}

#if defined(CMACPDF_SupportPS)
	if(mDeletePDF)
		::FSpDelete(&mPDFSpec);
#endif
}

#pragma mark -

#if defined(CMACPDF_SupportXML)

OSErr
CMacPDFParser::ConvertToXML(
	bool inUsePropertyListFormat)
{
	if(inUsePropertyListFormat) {
		CXMLNode* pdfNode = new CXMLNode("plist");
		pdfNode->SetAttribute("version", "1.0");
		CXMLTree::Push(pdfNode);
		
		CXMLTree::Push("dict");
	}

#if defined(CMACPDF_XMLWriteCatalog)
	CGPDFDictionaryRef catalog = ::CGPDFDocumentGetCatalog(mPDF);
	if(catalog) {
		if(inUsePropertyListFormat) {
			CXMLTree::PushAndPop("key", "Catalog");
			CXMLTree::Push("dict");
			::CGPDFDictionaryApplyFunction(catalog, DictionaryToPLIST, NULL); 
			CXMLTree::Pop();
		}
		else {
			CXMLNode* catalogNode = new CXMLNode("dictionary");
			catalogNode->SetAttribute("key", "Catalog");
			CXMLTree::Push(catalogNode);
			::CGPDFDictionaryApplyFunction(catalog, DictionaryToXML, NULL);
			CXMLTree::Pop();
		}
	}
#else	
	if(inUsePropertyListFormat == false) {
		CXMLNode* pdfNode = new CXMLNode("dictionary");
		pdfNode->SetAttribute("key", "Document");
		CXMLTree::Push(pdfNode);
	}

	size_t pages = GetPageCount();

	for(size_t i = 1; i <= pages; i++) {
		CGPDFPageRef page = ::CGPDFDocumentGetPage(mPDF, i);
		
		if(page) {
			::CGPDFPageRetain(page);

			if(inUsePropertyListFormat) {
				char str[256];
				sprintf(str, "Page %d", i);
				CXMLTree::PushAndPop("key", str);
				CXMLTree::Push("dict");
			}
			else {
				CXMLNode* pageNode = new CXMLNode("dictionary");
				pageNode->SetAttribute("key", "Page");
				CXMLTree::Push(pageNode);
			}
							
			CGPDFDictionaryRef dictionary = ::CGPDFPageGetDictionary(page);
			if(dictionary) {
				if(inUsePropertyListFormat)
					::CGPDFDictionaryApplyFunction(dictionary, DictionaryToPLIST, dictionary); 
				else
					::CGPDFDictionaryApplyFunction(dictionary, DictionaryToXML, dictionary); 
			}
			
			CXMLTree::Pop();
							
			::CGPDFPageRelease(page);
		}
	}

	if(inUsePropertyListFormat == false)
		CXMLTree::Pop();
#endif // CMACPDF_XMLWriteCatalog
	
	if(inUsePropertyListFormat)
		CXMLTree::Pop();
				
	return kNoError;
}

#endif // CMACPDF_SupportXML

void
CMacPDFParser::GetOutputFile(
	FSRefPtr inFile,
	FSRefPtr outFile,
	HFSUniStr255 inExtension)
{
#if defined(CMACPDF_SupportGUI)
	::BlockMoveData(inFile, outFile, sizeof(FSSpec));

	{ // 1.3
		short refNum;
		OSErr inErr = ::FSpOpenDF(inFile, (SInt8) fsRdWrPerm, &refNum);
		
		if(inErr == noErr)
			::FSClose(refNum);
		else if(inErr == fnfErr || inErr == wrPermErr || inErr == kWriteProtectedErr || inErr == afpAccessDenied) {			
			SInt16	theVRef;
			SInt32	theDirID;

			if(::FindFolder(kOnSystemDisk, kDesktopFolderType, true, &theVRef, &theDirID) == noErr) {
				outFile->vRefNum = theVRef;
				outFile->parID = theDirID;
				
				mDesktopConvert = true;
			}	
		}
	}
	
	Str255 outName;
	//LString::CopyPStr(inFile->name, outName);
	memcpy(outName, inFile->name, inFile->name[0] + 1);
	
	if(outName[0] + inExtension[0] > 32) {
		outName[0] = 32 - inExtension[0];
		
		if(outName[outName[0]] == ' ')
			outName[0]--;
			
		if(outName[outName[0]] == '.')
			outName[0]--;
	}
	
	if(CPreferences::GetPDFExtension() == false) {
		LStr255 testName(outName);
		
		if(outName[0] > 4 && testName.EndsWith(StringLiteral_(".pdf")))
			outName[0] -= 4;
	}
	
	LStr255 fileName(outName);
	
	fileName.Append(inExtension);	
	
	long fileCopyIndex = 0;
	OSErr err;
		
	do {
		LStr255 fileName(outName);
		
		if(fileCopyIndex) {
			fileName += '.';
			fileName += fileCopyIndex;
		}
		
		fileName.Append(inExtension);	

		//LString::CopyPStr(fileName, outFile->name);
		memcpy(outFile->name, fileName, fileName[0] + 1);

#ifdef DEBUG
		::FSpDelete(outFile);
#endif
		
		FSRef ref;
		err = ::FSpMakeFSRef(outFile, &ref);
		
		fileCopyIndex++;
	} while(err == noErr && fileCopyIndex < 8);
#endif // CMACPDF_SupportGUI
}

bool
CMacPDFParser::AddObject(
	CGPDFObjectRef inObject,
	ArrayIndexT& outIndex)
{
#if defined(__PowerPlant__)
	{
		LFastArrayIterator iterator(*sObjects);
		CGPDFObjectRef ref;

		while(iterator.Next(&ref)) {
			if(ref == inObject) {
				outIndex = iterator.GetCurrentIndex();
				return false;
			}	
		}
	}
			
	sObjects->AddItem(&inObject);
	
	outIndex = sObjects->GetCount();
#else
	std::vector<CGPDFObjectRef>::const_iterator i = sObjects.begin();
	CGPDFObjectRef s;
	
	ArrayIndexT index = 1;
	for(i = sObjects.begin(); i != sObjects.end(); i++) {
		s = *i;			
		
		if(s == inObject) {
			outIndex = index;
			return false;
		}
		
		index++;
	}
	
	sObjects.push_back(inObject);
	outIndex = (ArrayIndexT) sObjects.size();
#endif

	return true;
}

void
CMacPDFParser::ResetObjects()
{
#if defined(__PowerPlant__)
	if(sObjects) {
		delete sObjects;
		sObjects = NULL;
	}

	sObjects = new LArray;
#else
	sObjects.clear();
#endif
}

#pragma mark -

void
CMacPDFParser::DictionaryToText(
	const char *key,
	CGPDFObjectRef value,
	void *info)
{
	CMacPDFParser* parser = (CMacPDFParser*) info;
	if(parser && parser->DidAbort())
		return;

#if defined(CMACPDF_SupportGUI)
	UGlobalUtilities::NoSpinDelay();
#endif

	ArrayIndexT a;
	
	if(AddObject(value, a)) {
		CGPDFObjectType ot = ::CGPDFObjectGetType(value);
		
		if(ot == kCGPDFObjectTypeArray) {		
			if(key && strcmp(key, "Kids") == 0)
				return;

			CGPDFArrayRef array = NULL;
			::CGPDFObjectGetValue(value, ot, &array);
			
			if(array) {
				size_t objectCount = ::CGPDFArrayGetCount(array);

				if(key && strcmp(key, "Contents") == 0 && parser) 
					parser->Store();
				
				for(size_t i = 0; i < objectCount; i++) {
					CGPDFObjectRef object = NULL;
					if(::CGPDFArrayGetObject(array, i, &object))
						DictionaryToText(key, object, info);			
				}

				if(key && strcmp(key, "Contents") == 0 && parser) 
					parser->Release();
			}
		}
		else if(ot == kCGPDFObjectTypeDictionary) {
			bool isXObject = false;
			bool isResource = false;
			
			if(key) {
				if(strcmp(key, "Parent") == 0 || strcmp(key, "Annots") == 0)
					return;
				
				// XObject support
				if(strcmp(key, "XObject") == 0 && parser)
					isXObject = true;
					
				if(strcmp(key, "Resources") == 0 && parser)
					isResource = true;
			}

			CGPDFDictionaryRef dictionary = NULL;
			::CGPDFObjectGetValue(value, ot, &dictionary);
			
			if(dictionary) {
				if(isXObject)
					::CGPDFDictionaryApplyFunction(dictionary, DictionaryToXObject, info);	
				else if(isResource)
					::CGPDFDictionaryApplyFunction(dictionary, DictionaryToText, info);				
			}
		}
		else if(ot == kCGPDFObjectTypeStream) {
			if(key && strcmp(key, "Contents") == 0) {
				CGPDFStreamRef stream = NULL;
				::CGPDFObjectGetValue(value, ot, &stream);

				if(stream) {
					CGPDFDataFormat format = CGPDFDataFormatRaw;
					CFDataRef dr = ::CGPDFStreamCopyData(stream, &format);
					if(dr) {
						if(parser)
							parser->Parse(::CFDataGetBytePtr(dr), ::CFDataGetLength(dr));
							
						::CFRelease(dr);
					}
				}
			}
		}
	}
}

// XObject support
void
CMacPDFParser::DictionaryToXObject(
	const char *key,
	CGPDFObjectRef value,
	void *info)
{
	CMacPDFParser* parser = (CMacPDFParser*) info;
	if(parser && parser->DidAbort())
		return;

#if defined(CMACPDF_SupportGUI)
	UGlobalUtilities::NoSpinDelay();
#endif

	ArrayIndexT a;
	if(AddObject(value, a)) {
		CGPDFObjectType ot = ::CGPDFObjectGetType(value);
		
		if(ot == kCGPDFObjectTypeStream) {
			if(key && strncmp(key, "Fm", 2) == 0) {
				CGPDFStreamRef stream = NULL;
				::CGPDFObjectGetValue(value, ot, &stream);

				if(stream) {
					CGPDFDataFormat format = CGPDFDataFormatRaw;
					CFDataRef dr = ::CGPDFStreamCopyData(stream, &format);
					if(dr) {
						if(parser)
							parser->AddXObject(key, ::CFDataGetBytePtr(dr), ::CFDataGetLength(dr));
							
						::CFRelease(dr);
					}
				}
			}
		}
	}
}

void
CMacPDFParser::CatalogToFonts(
	const char *key,
	CGPDFObjectRef value,
	void *info)
{
	CMacPDFParser* parser = (CMacPDFParser*) info;
	if(parser && parser->DidAbort())
		return;
		
#if defined(CMACPDF_SupportGUI)
	UGlobalUtilities::NoSpinDelay();
#endif

	ArrayIndexT a;
	
	if(AddObject(value, a)) {
		CGPDFObjectType ot = ::CGPDFObjectGetType(value);
		
		if(ot == kCGPDFObjectTypeArray) {		
			if(key && strcmp(key, "Kids") != 0)
				return;

			CGPDFArrayRef array = NULL;
			::CGPDFObjectGetValue(value, ot, &array);
			
			if(array) {
				size_t objectCount = ::CGPDFArrayGetCount(array);
									
				for(size_t i = 0; i < objectCount; i++) {
					CGPDFObjectRef object = NULL;
					if(::CGPDFArrayGetObject(array, i, &object))
						CatalogToFonts(key, object, info);			
				}
			}
		}
		else if(ot == kCGPDFObjectTypeDictionary) {
			if(key) {
				if(strcmp(key, "Parent") == 0 || strcmp(key, "Annots") == 0)
					return;
			}

			CGPDFDictionaryRef dictionary = NULL;
			::CGPDFObjectGetValue(value, ot, &dictionary);
			
			if(dictionary) {
				if(key && info) {
					const char* p = NULL;
					if(::CGPDFDictionaryGetName(dictionary, "Type", &p) && strcmp(p, "Font") == 0)
						ExtractFont(key, dictionary, (CMacPDFParser*) info);
				}
									
				::CGPDFDictionaryApplyFunction(dictionary, CatalogToFonts, info);				
			}
		}
	}
}

void
CMacPDFParser::ExtractFont(
	const char* key,
	CGPDFDictionaryRef dictionary,
	CMacPDFParser* parser)
{
	const char* baseFont = NULL;
	const char* encoding = NULL;
	CGPDFDictionaryRef encodingDictionary = NULL;
	CGPDFStreamRef encodingStream = NULL;

	float* widths = NULL;
	char* cmap = NULL;	
	char* map = NULL;
	wchar_t* umap = NULL;
			
	CGPDFDictionaryRef fontDictionary = NULL;
	CGPDFStringRef charset = NULL;
	if(::CGPDFDictionaryGetDictionary(dictionary, "FontDescriptor", &fontDictionary)) {
		if(::CGPDFDictionaryGetString(fontDictionary, "CharSet", &charset)) {
			cmap = ExtractCharSet(charset);
		}
	}
	
	if(::CGPDFDictionaryGetName(dictionary, "BaseFont", &baseFont)) {
		CGPDFArrayRef widthsArray;
		if(::CGPDFDictionaryGetArray(dictionary, "Widths", &widthsArray)) {
			CGPDFInteger firstChar = 0;
			::CGPDFDictionaryGetInteger(dictionary, "FirstChar", &firstChar);
			
			size_t objectCount = ::CGPDFArrayGetCount(widthsArray);
			
			if(objectCount)
				widths = (float*) calloc(256, sizeof(float));
				
			if(widths) {
				for(size_t i = 0; i < objectCount; i++) {
					CGPDFObjectRef object = NULL;
					if(::CGPDFArrayGetObject(widthsArray, i, &object)) {
						CGPDFObjectType ot = ::CGPDFObjectGetType(object);
						
						if(ot == kCGPDFObjectTypeInteger) {
							CGPDFInteger w;
							if(::CGPDFArrayGetInteger(widthsArray, i, &w))
								widths[firstChar + i] = w;
						}
					}
				}
			}
		} 
				
		if(::CGPDFDictionaryGetName(dictionary, "Encoding", &encoding)) {
			// type 1, true type, type 3
			
			if(::CGPDFDictionaryGetStream(dictionary, "ToUnicode", &encodingStream)) {
				umap = ExtractUnicodeMap(encodingStream);
				
				if(umap) {
					map = FoldMaps(umap, cmap);
					
					if(UnicodeMapIsValid(umap) == false) {
						free(umap);
						umap = NULL;
					}
				}
			}
			
			if(map == NULL && umap == NULL && cmap) {
				map = cmap;
				cmap = NULL;
			}
				
			parser->AddFont(key, baseFont, encoding, map, umap, widths);
		}	
		else if(::CGPDFDictionaryGetStream(dictionary, "Encoding", &encodingStream)) {
			// type 0
			
			map = ExtractCharMap(encodingStream);
			
			if(map == NULL && cmap) {
				map = cmap;
				cmap = NULL;
			}
				
			parser->AddFont(key, baseFont, encoding, map, umap, widths);
		}
		else if(::CGPDFDictionaryGetDictionary(dictionary, "Encoding", &encodingDictionary)) {
			// type 1, true type, type 3
			
			bool symbolEncoding = false;
			bool dingbatEncoding = false;
			bool windowsEncoding = false;
			bool changeEncoding = false;
			
			if(strstr(baseFont, "Symbol"))
				symbolEncoding = true;
			else if(strstr(baseFont, "Dingbat"))
				dingbatEncoding = true;
			else {
				const char* baseEncoding = NULL;

				if(::CGPDFDictionaryGetName(encodingDictionary, "BaseEncoding", &baseEncoding)) {
					if(strcmp(baseEncoding, "WinAnsiEncoding") == 0)
						windowsEncoding = true;
				}
			}

#if defined(__PowerPlant__)
			parser->SetEncodeForWord(CPreferences::GetWord2004());
#endif
	
			parser->SetEncodeForWord(true);
		
			if((symbolEncoding || dingbatEncoding || windowsEncoding) && (parser->GetEncodeForWord() || parser->GetType() != kWriteRTFWord))
				changeEncoding = true;

			char customEncoding[256] = "MacRomanEncoding";
			if(changeEncoding) {
				if(symbolEncoding)
					strcpy(customEncoding, "MacSymbolEncoding");
				else if(dingbatEncoding)
					strcpy(customEncoding, "MacDingbatEncoding");
				else if(windowsEncoding)
					strcpy(customEncoding, "WinAnsiEncoding");
			}	
			
			unsigned char fi = 0; // ligature support
			unsigned char fl = 0;
			
			bool adobe = false; // adobe aNNN char code support

			CGPDFArrayRef array;
			if(::CGPDFDictionaryGetArray(encodingDictionary, "Differences", &array)) { // rare for true type
				UPDFMaps::ConstMapParam converter = UPDFMaps::kMacLatinMap;
				if(symbolEncoding)
					converter = UPDFMaps::kSymbolMap;
				else if(windowsEncoding)
					converter = UPDFMaps::kWinLatinMap;

				map = (char*) malloc(256);
				if(map) {
					for(long i = 0; i < 256; i++)
						map[i] = (cmap ? cmap[i] : i);
						
					long doff = 0;
					
					size_t objectCount = ::CGPDFArrayGetCount(array);
										
					for(size_t i = 0; i < objectCount; i++) {
						CGPDFObjectRef object = NULL;
						if(::CGPDFArrayGetObject(array, i, &object)) {
							CGPDFObjectType ot = ::CGPDFObjectGetType(object);
							
							if(ot == kCGPDFObjectTypeInteger) {
								CGPDFInteger doffInt = 0;
								::CGPDFObjectGetValue(object, ot, &doffInt);
								
								doff = doffInt;
							}
							else if(ot == kCGPDFObjectTypeName) {
								const char* p = NULL;
								::CGPDFArrayGetName(array, i, &p);
								
								if(p) {
									char c = UPDFMaps::NameToCode(converter, p);
									if(c)
										map[doff] = c;
									else if(strcmp(p, "fi") == 0)
										fi = doff;
									else if(strcmp(p, "fl") == 0)
										fl = doff;
									else {
										c = UPDFMaps::AdobeNameToCode(p);
										
										if(c) {
											map[doff] = c;
											adobe = true;
										}
									}
										
									doff++;
								}
							}
						}	
					}
				}
			}

			if(fi || fl) 
				strcpy(customEncoding, "WinAnsiEncoding");
			else if(adobe)
				strcpy(customEncoding, "MacRomanEncoding");

			if(::CGPDFDictionaryGetStream(dictionary, "ToUnicode", &encodingStream)) {
				umap = ExtractUnicodeMap(encodingStream);
				
				if(umap) {
					map = FoldMaps(umap, map);
					
					if(UnicodeMapIsValid(umap) == false) {
						free(umap);
						umap = NULL;
					}
				}
			}
			
			if(map == NULL && umap == NULL && cmap) {
				map = cmap;
				cmap = NULL;
			}
				
			parser->AddFont(key, baseFont, customEncoding, map, umap, widths, fi, fl);
		}
		else {
			if(::CGPDFDictionaryGetStream(dictionary, "ToUnicode", &encodingStream)) {
				umap = ExtractUnicodeMap(encodingStream);
				
				if(umap) {
					map = FoldMaps(umap, cmap);
					
					if(UnicodeMapIsValid(umap) == false) {
						free(umap);
						umap = NULL;
					}
				}
			}
				
			if(map == NULL && umap == NULL && cmap) {
				map = cmap;
				cmap = NULL;
			}

			/* test
			if(map == NULL) {
				map = (char*) malloc(256);
				for(long i = 0; i < 256; i++) {
					map[i] = i;
				}
				for(long i = 1; i < 64; i++) {
					map[i] = 'A' + i;
				}
			}*/
			
			parser->AddFont(key, baseFont, encoding, map, umap, widths);
		}
	}
	
	if(cmap)
		free(cmap);
}

wchar_t*
CMacPDFParser::ExtractUnicodeMap(
	CGPDFStreamRef stream)
{
	wchar_t* map = NULL;
	
	return NULL;
	
	if(stream) {
		CGPDFDataFormat format = CGPDFDataFormatRaw;
		CFDataRef dr = ::CGPDFStreamCopyData(stream, &format);
		if(dr) {
			const char* dataPtr = (char*) ::CFDataGetBytePtr(dr);
			CFIndex dataLength = ::CFDataGetLength(dr);
				
			for(CFIndex i = 0; i < dataLength; i++) {
				if(i < dataLength - 12 && strncmp(&(dataPtr[i]), "beginbfrange", 12) == 0) {
					bool inMap = true;
				
					while(i < dataLength && isspace(dataPtr[i]) == false)
						i++;
					
					i++;
								
					do {
						if(i < dataLength - 10 && strncmp(&(dataPtr[i]), "endbfrange", 10) == 0)
							inMap = false;
						else {
							if(map == NULL)
								map = (wchar_t*) calloc(256, sizeof(wchar_t));
							
							if(map) {
								long start = 0;
								long end = 0;
								long u = 0;
								
								{
									bool hex = false;
									if(dataPtr[i] == '<') {
										i++;
										hex = true;
									}
									
									char num[16];
									long n = 0;
									while(i < dataLength && isalnum(dataPtr[i]))
										num[n++] = dataPtr[i++];
								
									num[n] = 0;
									
									if(hex)
										i++;
									
									i++;
									
									if(n) {
										long swap = CFSwapInt32BigToHost(strtol(num, (char**) NULL, (hex ? 16 : 10)));
										start = swap;
									}
								}	

								{
									bool hex = false;
									if(dataPtr[i] == '<') {
										i++;
										hex = true;
									}
									
									char num[16];
									long n = 0;
									while(i < dataLength && isalnum(dataPtr[i]))
										num[n++] = dataPtr[i++];
								
									num[n] = 0;
									
									if(hex)
										i++;
									
									i++;
									
									if(n) {
										long swap = CFSwapInt32BigToHost(strtol(num, (char**) NULL, (hex ? 16 : 10)));
										end = swap;
									}
								}	

								if(dataPtr[i] == '[') {
									CFIndex k = i;
									while(k < dataLength && dataPtr[k] != ']')
										k++;
										
									if(k > i + 1 && k < dataLength) {
										long len = (k - i) - 1;
										long* uarray = ExtractArray((char*) &(dataPtr[i + 1]), &len);
										if(uarray) {
											// version 1.0 does not support arrays inside UnicodeMaps
											free(uarray);
										}
										
										i = k + 2;
									}
								}
								else {
									bool hex = false;
									if(dataPtr[i] == '<') {
										i++;
										hex = true;
									}
									
									char num[16];
									long n = 0;
									while(i < dataLength && isalnum(dataPtr[i]))
										num[n++] = dataPtr[i++];
								
									num[n] = 0;
									
									if(hex)
										i++;
									
									i++;
									
									if(n) {
										long swap = CFSwapInt32BigToHost(strtol(num, (char**) NULL, (hex ? 16 : 10)));
										u = swap;
								
										if(start >= 0 && start <= 255 && end >= 0 && end <= 255) {
											for(long x = start; x <= end; x++) {
												if(u >= 0 && u <= 255) // necessary?
													map[x] = u;
													
												u++;	
											}	
										}
									}
								}
							}
						}
					} while(i < dataLength && inMap);
				}			
				else if(i < dataLength - 11 && strncmp(&(dataPtr[i]), "beginbfchar", 11) == 0) {
					bool inMap = true;
				
					while(i < dataLength && isspace(dataPtr[i]) == false)
						i++;
					
					i++;
								
					do {
						if(i < dataLength - 9 && strncmp(&(dataPtr[i]), "endbfchar", 9) == 0)
							inMap = false;
						else {
							if(map == NULL)
								map = (wchar_t*) calloc(256, sizeof(wchar_t));
							
							if(map) {
								long start = 0;
								long u = 0;
								
								{
									bool hex = false;
									if(dataPtr[i] == '<') {
										i++;
										hex = true;
									}
									
									char num[16];
									long n = 0;
									while(i < dataLength && isalnum(dataPtr[i]))
										num[n++] = dataPtr[i++];
								
									num[n] = 0;
									
									if(hex)
										i++;
									
									i++;
									
									if(n) {
										long swap = CFSwapInt32BigToHost(strtol(num, (char**) NULL, (hex ? 16 : 10)));
										start = swap;
									}
								}	

								{
									bool hex = false;
									if(dataPtr[i] == '<') {
										i++;
										hex = true;
									}
																		
									char num[16];
									long n = 0;
									while(i < dataLength && isalnum(dataPtr[i]))
										num[n++] = dataPtr[i++];
								
									num[n] = 0;
									
									if(hex)
										i++;
									
									i++;
									
									if(n) {
										long swap = CFSwapInt32BigToHost(strtol(num, (char**) NULL, (hex ? 16 : 10)));
										u = swap;
									}
								}
								
								if(start >= 0 && start <= 255 && u >= 0 && u <= 255) // necessary?
									map[start] = u;
							}
						}
					} while(i < dataLength && inMap);
				}			
			}	
				
			::CFRelease(dr);
		}
	}
		
	return map;
}

char*
CMacPDFParser::ExtractCharMap(
	CGPDFStreamRef stream)
{
#pragma unused (stream)

	// unimplemented in 1.0
	
	return NULL;
}

char*
CMacPDFParser::ExtractCharSet(
	CGPDFStringRef stream,
	UPDFMaps::ConstMapParam pdfMap)
{
	char* map = NULL;	
	
	long entry = 0;
	
	if(stream) {
		map = (char*) malloc(256);
		
		if(map) {
			for(long j = 0; j < 256; j++)
				map[j] = j;

			size_t l = ::CGPDFStringGetLength(stream);
			unsigned char* c = (unsigned char*) ::CGPDFStringGetBytePtr(stream);
			
			if(c) {
				char name[32];
				char nindex = 0;
				
				for(size_t i = 0; i < l; i++) {
					if(c[i] == '/' || i == l - 1) {
						if(nindex) {
							name[nindex] = 0;
							char cm = UPDFMaps::NameToCode(pdfMap, name);
							if(cm)
								map[entry] = cm;
																						
							entry++;
						}
						
						nindex = 0;
					}
					else
						name[nindex++] = c[i];
				}
			}
		}
	}
	
	return map;
}

long*
CMacPDFParser::ExtractArray(
	char* c,
	long* l)
{
#pragma unused (c, l)

	return NULL;
}	

char*
CMacPDFParser::FoldMaps(
	wchar_t* unicode,
	char* ansi,
	TextEncoding encoding)
{
	char* map = ansi;
	
	for(long i = 0; i < 256; i++) {
		short u = unicode[i];
		
		if(u) {
			unsigned char ustring[2];
			memcpy(&(ustring[0]), &u, 2);
			
			CFStringRef sr = ::CFStringCreateWithBytes(kCFAllocatorDefault, ustring, 2, kCFStringEncodingUnicode, false);
			if(sr) {
				Str255 buffer;
				if(::CFStringGetPascalString(sr, buffer, 256, encoding) && buffer[0] == 1) {
					if(map == NULL) {
						map = (char*) malloc(256);
						
						if(map) {
							for(long j = 0; j < 256; j++)
								map[j] = j;
						}
					}
					
					if(map) {
						map[i] = buffer[1];
						unicode[i] = 0;
					}
				}
				
				::CFRelease(sr);
			}
		}
	}
		
	return map;
}

bool
CMacPDFParser::UnicodeMapIsValid(
	wchar_t* map)
{
	for(long i = 0; i < 256; i++) {
		if(map[i])
			return true;
	}
	
	return false;
}

bool
CMacPDFParser::CharMapIsValid(
	char* map)
{
	for(long i = 0; i < 256; i++) {
		if(map[i])
			return true;
	}
	
	return false;
}

#pragma mark -

#if defined(CMACPDF_SupportXML)

void
CMacPDFParser::DictionaryToXML(
	const char *key,
	CGPDFObjectRef value,
	void *info)
{
	IndexDictionaryToXML(key, value, info, -1);
}

void
CMacPDFParser::IndexDictionaryToXML(
	const char *key,
	CGPDFObjectRef value,
	void *info,
	size_t index)
{
	CXMLNode* keyNode = NULL;

	CGPDFObjectType ot = ::CGPDFObjectGetType(value);
	
	switch(ot) {
		case kCGPDFObjectTypeNull:
			keyNode = new CXMLNode("null");
			break;
			
		case kCGPDFObjectTypeBoolean:
			keyNode = new CXMLNode("boolean");
			break;
			
		case kCGPDFObjectTypeInteger:
			keyNode = new CXMLNode("integer");
			break;
			
		case kCGPDFObjectTypeReal:
			keyNode = new CXMLNode("real");
			break;
			
		case kCGPDFObjectTypeName:	
			keyNode = new CXMLNode("name");
			break;
		
		case kCGPDFObjectTypeString:
			keyNode = new CXMLNode("string");
			break;
			
		case kCGPDFObjectTypeArray:
			keyNode = new CXMLNode("array");
			break;
			
		case kCGPDFObjectTypeDictionary:
			keyNode = new CXMLNode("dictionary");
			break;
			
		case kCGPDFObjectTypeStream:
			keyNode = new CXMLNode("stream");
			break;
			
		default:	
			return;
	}
	
	if(key && index == -1)
		keyNode->SetAttribute("key", (char*) key);

	switch(ot) {
		case kCGPDFObjectTypeBoolean:
		{
			bool b = 0;
			::CGPDFObjectGetValue(value, ot, &b);
			
			if(b)
				keyNode->SetData("true");
			else
				keyNode->SetData("false");
			break;
		}
			
		case kCGPDFObjectTypeInteger:
		{
			CGPDFInteger i = 0;
			::CGPDFObjectGetValue(value, ot, &i);
			
			char str[256];
			sprintf(str, "%d", i);
			keyNode->SetData(str);
			break;
		}	

		case kCGPDFObjectTypeReal:
		{
			CGPDFReal r = 0;
			::CGPDFObjectGetValue(value, ot, &r);
			
			char str[256];
			sprintf(str, "%f", r);
			keyNode->SetData(str);
			break;
		}	
					
		case kCGPDFObjectTypeName:
		{
			const char* p = NULL;
			
			if(index == -1) {
				// parent is dictionary
				CGPDFDictionaryRef dictionary = (CGPDFDictionaryRef) info;
				if(dictionary)
					::CGPDFDictionaryGetName(dictionary, key, &p);
			}
			else {
				// parent is array
				CGPDFArrayRef array = (CGPDFArrayRef) info;
				if(array)
					::CGPDFArrayGetName(array, index, &p);
			}
			
			if(p)
				keyNode->SetData(p);
			else
				keyNode->SetData("Catalog"); // this is an assumption
				
			break;
		}
		
		case kCGPDFObjectTypeString:
		{
			CGPDFStringRef s = NULL;
			::CGPDFObjectGetValue(value, ot, &s);
			
			if(s)
				keyNode->SetData((char*) ::CGPDFStringGetBytePtr(s), ::CGPDFStringGetLength(s));
					
			break;
		}	

		case kCGPDFObjectTypeArray:
			/*if(key && strcmp(key, "Kids") == 0) {
				delete keyNode;
				keyNode = NULL;
				break;
			}*/
			break;
			
		case kCGPDFObjectTypeDictionary:
			if(key && strcmp(key, "Parent") == 0) {
				delete keyNode;
				keyNode = NULL;
			}	
			break;
	}
	
	if(keyNode) {	
		CXMLTree::Push(keyNode);
		
		if(ot == kCGPDFObjectTypeArray) {		
			CGPDFArrayRef array = NULL;
			::CGPDFObjectGetValue(value, ot, &array);
			
			if(array) {
				size_t objectCount = ::CGPDFArrayGetCount(array);
									
				for(size_t i = 0; i < objectCount; i++) {
					CGPDFObjectRef object = NULL;
					if(::CGPDFArrayGetObject(array, i, &object))
						IndexDictionaryToXML(key, object, array, i);			
				}
			}
		}
		else if(ot == kCGPDFObjectTypeDictionary) {
			ArrayIndexT a;
			
			if(AddObject(value, a) == false)
				keyNode->SetAttribute("reference", a);
			else {
				keyNode->SetAttribute("object", a);
					
				CGPDFDictionaryRef dictionary = NULL;
				::CGPDFObjectGetValue(value, ot, &dictionary);
				
				if(dictionary)
					::CGPDFDictionaryApplyFunction(dictionary, DictionaryToXML, dictionary);
			}
		}
		else if(ot == kCGPDFObjectTypeStream) {
			CGPDFStreamRef stream = NULL;
			::CGPDFObjectGetValue(value, ot, &stream);

			if(stream) {
				CGPDFDataFormat format = CGPDFDataFormatRaw;
				CFDataRef dr = ::CGPDFStreamCopyData(stream, &format);
				if(dr) {
					if(key && (strcmp(key, "Contents") == 0 || strcmp(key, "ToUnicode") == 0))
						keyNode->SetCData((char*) ::CFDataGetBytePtr(dr), ::CFDataGetLength(dr));
					else {
						//if(sXMLStripBinary) {
							char noData[256] = "[STRIPPED]";
							keyNode->SetData(noData, strlen(noData));
						//}
						//else
						//	keyNode->Set64Data((char*) ::CFDataGetBytePtr(dr), ::CFDataGetLength(dr));
					}	
						
					::CFRelease(dr);
				}
			}
		}
		
		CXMLTree::Pop();
	}	
}

void
CMacPDFParser::DictionaryToPLIST(
	const char *key,
	CGPDFObjectRef value,
	void *info)
{
	IndexDictionaryToPLIST(key, value, info, -1);
}

void
CMacPDFParser::IndexDictionaryToPLIST(
	const char *key,
	CGPDFObjectRef value,
	void *info,
	size_t index)
{
#pragma unused(info)

	CGPDFObjectType ot = ::CGPDFObjectGetType(value);
	
	switch(ot) {
		case kCGPDFObjectTypeBoolean:
		case kCGPDFObjectTypeInteger:
		case kCGPDFObjectTypeReal:
		case kCGPDFObjectTypeName:
		case kCGPDFObjectTypeString:
		case kCGPDFObjectTypeStream:
			break;
			
		case kCGPDFObjectTypeArray:
			/*if(key && strcmp(key, "Kids") == 0)
				return;*/
				
			break;
			
		case kCGPDFObjectTypeDictionary:
			if(key && strcmp(key, "Parent") == 0)
				return;
				
			break;
			
		default:	
			return;
	}

	if(key && index == -1)
		CXMLTree::PushAndPop("key", (char*) key);
		
	CXMLNode* keyNode = NULL;
	
	switch(ot) {
		case kCGPDFObjectTypeBoolean:
		{
			bool b = 0;
			::CGPDFObjectGetValue(value, ot, &b);
			
			if(b)
				keyNode = new CXMLNode("true");		
			else
				keyNode = new CXMLNode("false");		
			break;
		}
			
		case kCGPDFObjectTypeInteger:
		{
			CGPDFInteger i = 0;
			::CGPDFObjectGetValue(value, ot, &i);
			
			keyNode = new CXMLNode("integer");
			char str[256];
			sprintf(str, "%d", i);
			keyNode->SetData(str);
			break;
		}	
		break;

		case kCGPDFObjectTypeReal:
		{
			CGPDFReal r = 0;
			::CGPDFObjectGetValue(value, ot, &r);
			
			keyNode = new CXMLNode("real");
			char str[256];
			sprintf(str, "%f", r);
			keyNode->SetData(str);
			break;
		}	
			
		case kCGPDFObjectTypeName:
		{
			const char* p = NULL;
			
			if(index == -1) {
				// parent is dictionary
				CGPDFDictionaryRef dictionary = (CGPDFDictionaryRef) info;
				if(dictionary)
					::CGPDFDictionaryGetName(dictionary, key, &p);
			}
			else {
				// parent is array
				CGPDFArrayRef array = (CGPDFArrayRef) info;	
				if(array)
					::CGPDFArrayGetName(array, index, &p);
			}
			
			keyNode = new CXMLNode("string");
			
			if(p)
				keyNode->SetData(p);
			else
				keyNode->SetData("Catalog"); // this is an assumption
			
			break;
		}

		case kCGPDFObjectTypeString:
		{
			keyNode = new CXMLNode("string");

			CGPDFStringRef s = NULL;
			::CGPDFObjectGetValue(value, ot, &s);
			
			if(s)
				keyNode->SetData((char*) ::CGPDFStringGetBytePtr(s), ::CGPDFStringGetLength(s));
			else
				keyNode->SetData("[NULL]"); // should never happen

			break;
		}	

		case kCGPDFObjectTypeArray:
		{
			CGPDFArrayRef array = NULL;
			::CGPDFObjectGetValue(value, ot, &array);
			
			if(array) {			
				CXMLTree::Push("array");
				
				size_t objectCount = ::CGPDFArrayGetCount(array);
				for(size_t i = 0; i < objectCount; i++) {
					CGPDFObjectRef object = NULL;
					if(::CGPDFArrayGetObject(array, i, &object))
						IndexDictionaryToPLIST(key, object, array, i);			
				}

				CXMLTree::Pop();
			}
			
			break;
		}
			
		case kCGPDFObjectTypeDictionary:
		{			
			CGPDFDictionaryRef dictionary = NULL;
			::CGPDFObjectGetValue(value, ot, &dictionary);
			
			if(dictionary) {
				CXMLTree::Push("dict");
				
				ArrayIndexT a;
				
				if(AddObject(value, a) == false) {
					char str[256];
					sprintf(str, "%d", a);

					CXMLTree::PushAndPop("key", "_Reference");
					CXMLTree::PushAndPop("integer", str); 
				}
				else {
					char str[256];
					sprintf(str, "%d", a);

					CXMLTree::PushAndPop("key", "_Object");
					CXMLTree::PushAndPop("integer", str); 

					::CGPDFDictionaryApplyFunction(dictionary, DictionaryToPLIST, dictionary);
				}
				
				CXMLTree::Pop(); 
			}
			break;
		}
			
		case kCGPDFObjectTypeStream:
		{
			CGPDFStreamRef stream = NULL;
			::CGPDFObjectGetValue(value, ot, &stream);

			if(stream) {
				CXMLNode* streamNode = new CXMLNode("data");
		
				CGPDFDataFormat format = CGPDFDataFormatRaw;
				CFDataRef dr = ::CGPDFStreamCopyData(stream, &format);
				if(dr) {
					streamNode->Set64Data((char*) ::CFDataGetBytePtr(dr), ::CFDataGetLength(dr));
					::CFRelease(dr);
				}

				CXMLTree::PushAndPop(streamNode);
			}
			
			break;
		}
	}
	
	if(keyNode)
		CXMLTree::PushAndPop(keyNode);
}

#endif // CMACPDF_SupportXML

#if !defined(Obsolete10p4)
OSErr CMacPDFParser::FSWrite(FSIORefNum refNum, long *count, const void *buffPtr)
{
    return ::FSWriteFork(refNum, fsAtMark, 0, *count, buffPtr, NULL);
}
#endif

