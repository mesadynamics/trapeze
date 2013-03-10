// Trapeze
//
// Copyright (c) 2004 Mesa Dynamics, LLC.  All rights reserved.

#include "CPDFParser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

//#define SHOWCOORDS

CPDFParser::CPDFParser() :
		mAbort(false),
		mRestrict(false),
		mCol(0),
		mLine(0),
		mPadCols(true),
		mPadLines(true),
		mPadStrip(false),
		mRewrap(false),
		mRelaxSpacing(false),
		mTightSpacing(false),
		mSort(true),
		mNewlineCode(kNewlineUNIX),
		mFontChanges(true),
		mSizeChanges(true),
		mStyleChanges(true),
		mSuperSubChanges(true),
		mTabTable(NULL),
		mData(NULL),
		mDataSize(0),
		mPageWidth(0),
		mPageHeight(0),
		mPageLength(0),
		mPageWeight(0),
		mPageWidths(0),
		mPageSpacing(0),
		mEncodingOut(kEncodeMacRoman),
		mTopMargin(0),
		mLeftMargin(0),
		mParseIntoStore(false),
		mParseBytes(0)
{
}

CPDFParser::~CPDFParser()
{
	FreeTabs();

	FreeFonts();
	FreeXObjects();
	FreeEncoders();

	if(mData) {
		free(mData);
		mData = NULL;
	}
}

#pragma mark -

OSErr
CPDFParser::RenderPage(
	size_t inPage)
{
	float width = 0.0;
	float height = 0.0;
	
	mCrop = false;
	mCropWidth = 0.0;
	mCropHeight = 0.0;
	
	OSErr error = BeginRender(inPage, &width, &height);
	
	if(error != kNoError)
		return error;

	if(width < 72.0)
		width = 72.0;
		
	if(height < 72.0)
		height = 72.0;
		
	mPageWidth = lroundf(width);
	mPageHeight = lroundf(height);
	
	mPageLength = 0;
	mPageWeight = 0;
	mPageWidths = 0;
	mPageSpacing = 0;

	// set only at beginning of page (removed from InitMetrics())
	mF = 1.0;
	mFS = 1.0;

	mFont = NULL;
	mAllWhitespace = false;

	mQDepth = 0;

	for(long i = 0; i < kMaxQDepth; i++) {
		mSaveScale[i] = 1.0;
		mSaveDeltaX[i] = 0.0;
		mSaveDeltaY[i] = 0.0;
	}
		
	mScale = 1.0;
	mDeltaX = 0.0;
	mDeltaY = 0.0;
	
	InitChunker();
	InitMetrics();
		
	// subclasses should make calls to Parse() here
	Render();
	
	// only necessary if the PDF is malformed or a memory error resulted in an
	// unprocessed chunk of text
	CloseChunker();
		
	// setup environment	
	if(mCol == 0)
		mCol = lroundf(width / kCellWidth);
		
	if(mLine == 0)
		mLine = lroundf(height / kCellHeight);

	if(mPageLength) {
		// compute rendering grid dimensions
		if(mPageWeight) {
			long totalWidths = (mPageWidths == 0 ? mPageLength : mPageWidths);
			long averageWeight = (mPageWeight / totalWidths);
			
			long mod = (averageWeight % 4);
			if(mod)	
				averageWeight += (4 - mod);
				
			if(averageWeight < 4)
				averageWeight = 4;
			else if(averageWeight > 72)
				averageWeight = 72;
						
			mCol = lroundf((2.0 * width) / (float) averageWeight);
			mLine = lroundf(height / (float) averageWeight);
		}
			
		// initialize rendering parameters
		xmax = 0.0;
		ymax = 0.0;
		
		if(mCol < 12)
			mCol = 12;
			
		if(mLine < 6)
			mLine = 6;
			
		// normalize page space
		Normalize(width,  height);

		xs = (mCol == 0 ? 1.0 : ((float) mCol / (float) width));
		ys = (mLine == 0 ? 1.0 : ((float) mLine / (float) height));
		
		// sort text objects by location on page
		if(mSort)
			std::stable_sort(mPageObjects.begin(), mPageObjects.end(), CTextSorter());
		
		// fit objects into text space
		Fit();
		
		// find line breaks
		CalcLines();

		// insert tags if necessary
		if(mType >= kWriteRTF)
			ObjectsToRTF();
		else if(mType == kWriteHTML)
			ObjectsToHTML();
					
		// adjust for all additional whitespace
		mPageLength += CalcWhitespace();
		
		if(mType >= kWriteRTF)
			CalcExtraTabs();
		
		// allocate buffers
		if(mPageLength == 0)
			goto out;
		
		long pageBytes = mPageLength;
		if(pageBytes < 32767L)
			pageBytes = 32767L;
		else
			pageBytes += (32767L - (mPageLength % 32767L));
			
		mData = (unsigned char*) malloc(pageBytes);
		if(mData == NULL) {
			error = kMemoryError;
			goto out;
		}		
		
		unsigned char* newlineBuffer = (unsigned char*) malloc(mLine + 2);
		if(newlineBuffer == NULL) {
			free(mData);
			mData = NULL;
			
			error = kMemoryError;
			goto out;
		}
					
		unsigned char* spaceBuffer = (unsigned char*) malloc(mCol + 1);
		if(spaceBuffer == NULL) {
			free(newlineBuffer);
			
			free(mData);
			mData = NULL;
			
			error = kMemoryError;
			goto out;
		}
				
		for(long i = 0; i < mLine + 2; i++)
			newlineBuffer[i] = '\n';

		for(long i = 0; i < mCol + 1; i++)
			spaceBuffer[i] = ' ';
									
		// space trackers
		float currentX = 0.0;
		float currentY = ymax;
		float currentF = 0.0;
		long currentCol = 0;
		
		//long spaceCount = 0;
				
		// render
		{
			std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
			PDFTextObject* t;

			for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
				t = *i;

				if(t->text) {
					if(t->size) {
						if(t->line) {
							memcpy(&(mData[mDataSize]), newlineBuffer, t->line);
							mDataSize += t->line;

							currentCol = 0;

							if(t->col) {
								//if(mType == kWriteHTML)
								//	spaceCount = t->col;
								//else {
									memcpy(&(mData[mDataSize]), spaceBuffer, t->col);
									mDataSize += t->col;
								//}
								
								currentCol += t->col;
							}
						}
						else if(t->col && t->ws == false) {
							if(mType >= kWriteRTF) {
								if(mPadStrip == false && GetTabIndex(t->col) != -1 && t->x > currentX) {
									long tabs = GetTabsToCol(lroundf(currentX * xs), t->col);
									
									if(tabs > 0) {
										while(tabs-- > 0)	
											mData[mDataSize++] = '\t';	
									}							
								
									currentCol = t->col;
								}
								else if(t->col < currentCol) {
									while(
										mDataSize >= 2 &&
										mData[mDataSize - 1] == ' ' &&
										mData[mDataSize - 2] == ' ' &&
										t->col < currentCol
									) {
										mDataSize--;
										currentCol--;
									}
								}
							}
							else {
								if(t->col > currentCol) {	
									long offset = t->col - currentCol;
																	
									if(mRelaxSpacing || offset >= kMinMidlineSpacing) {
										if(mRelaxSpacing && mTightSpacing) { // 1.4
											if(offset > 1 && offset < kMinMidlineSpacing) {
												currentCol += (offset - 1);
												offset = 1;
											
												// use 2 for punctuation?
											}
										}
																			
										//if(mType == kWriteHTML)
										//	spaceCount = offset;
										//else {
											memcpy(&(mData[mDataSize]), spaceBuffer, offset);
											mDataSize += offset;
										//}
										
										currentCol += offset;
									}
								}
								else {
									while(
										mDataSize >= 2 &&
										mData[mDataSize - 1] == ' ' &&
										mData[mDataSize - 2] == ' ' &&
										t->col < currentCol
									) {
										mDataSize--;
										currentCol--;
									}
								}
							}
						}
					
						currentX = t->tx;
						currentY = t->y;
						currentF = t->f;

						if(t->pre) {
							memcpy(&(mData[mDataSize]), t->pre, t->preSize);
							mDataSize += t->preSize;
						}
						
						//if(spaceCount) {
						//	memcpy(&(mData[mDataSize]), spaceBuffer, spaceCount);
						//	mDataSize += spaceCount;
						//	
						//	spaceCount = 0;
						//}
																								
						memcpy(&(mData[mDataSize]), t->text, t->size);
						mDataSize += t->size;
						
						currentCol += t->width;
						
						if(t->post) {
							memcpy(&(mData[mDataSize]), t->post, t->postSize);
							mDataSize += t->postSize;
						}																	
					}
					
					free(t->text);
				}
									
				if(t->pre)
					free(t->pre);
					
				if(t->post)
					free(t->post);				

				free(t);
			}
		}
			
		if(mPageSpacing) {
			memcpy(&(mData[mDataSize]), newlineBuffer, mPageSpacing);
			mDataSize += mPageSpacing;
		}
		
		// free buffers
		free(spaceBuffer);	
		free(newlineBuffer);	
	}

out:
	if(error == kMemoryError) {
		std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
		PDFTextObject* t;

		for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
			t = *i;

			if(t->text)
				free(t->text);
				
			if(t->pre)
				free(t->pre);
				
			if(t->post)
				free(t->post);				
				
			free(t);
		}
	}
	
	mPageObjects.clear();
	
	EndRender();
		
	if(mData) {
		if(mPadStrip)
			Strip();
		else
			Clean();
			
		if(mRewrap)
			Rewrap();
			
		if(mType >= kWriteRTF)
			PageToRTF();
		else if(mType == kWriteHTML)
			PageToHTML();
		
		if(mType == kWriteASCII || mType == kWritePlainText || mType == kWriteHTML)
			FixNewlines();
	}
		
	return error;
}

void
CPDFParser::ObjectsToRTF()
{
	std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
	PDFTextObject* t;

	PDFFontObject* currentFont = NULL;
	long currentSize = 0;
	
	bool boldStyle = false;
	bool italicStyle = false;
	
	for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
		t = *i;

		if(t->text && t->size && t->ws == false) {
			bool changeFont = false;
			bool changeSize = false;
			bool changeBold = false;
			bool changeItalic = false;
			
			if(t->font) {
				if(t->font != currentFont) {
					changeFont = true;
					currentFont = t->font;
				}
				
				changeBold = t->font->bold;
				changeItalic = t->font->italic;
			}
			
			bool changeSuper = false;
			bool changeSub = false;
			
			if(fequal_(t->ty, t->y) == false) {
				if(t->ty > t->y)
					changeSuper = true;
				else if(t->ty < t->y)
					changeSub = true;
			}
			
			long fontSize = lroundf(t->f * 2.0);
			
			if(changeSuper || changeSub) {
				if(mType == kWriteRTFWord)
					fontSize = lroundf(t->f * 2.0 * kRTFWordScriptScale);
				else
					fontSize = lroundf(t->f * 2.0);
			}
				
			if(fontSize == 0)
				fontSize = 24;
				
			long lineSpacing = lroundf(t->f * 20.0 * kRTFSpacing);
	
			if(lineSpacing == 0)
				lineSpacing = lroundf(12.0 * 20.0 * kRTFSpacing);
					
			if(fontSize != currentSize) {
				changeSize = true;
				currentSize = fontSize;
			}
			
			if(changeFont || changeSize || changeSuper || changeSub) {
				char* pre = (char*) malloc(64);
				pre[0] = 0;

				char* post = NULL;
				if((changeSuper || changeSub) && pre) {
					post = (char*) malloc(64);
					post[0] = 0;
				}
				
				if(pre) {
					char buf[64];
					
					if(mStyleChanges) {
						if(changeBold == false && boldStyle) {
							strcat(pre, "\\b0");
							boldStyle = false;
						}
					
						if(changeItalic == false && italicStyle) {
							strcat(pre, "\\i0");
							italicStyle = false;
						}
					}
										
					if(mFontChanges && changeFont) {
						sprintf(buf, "\\f%d", (int)t->font->index);
						strcat(pre, buf);
					}
						
					if(mSizeChanges && changeSize) {
						sprintf(buf, "\\fs%d\\sl%d", (int)fontSize, (int)lineSpacing);
						strcat(pre, buf);
					}
					
					if(mStyleChanges) {
						if(changeBold && boldStyle == false) {
							strcat(pre, "\\b");
							boldStyle = true;
						}
					
						if(changeItalic && italicStyle == false) {
							strcat(pre, "\\i");
							italicStyle = true;
						}
					}
						
					if(post) {
						if(mSuperSubChanges) {
							if(changeSuper)
								strcat(pre, "\\super");
							else if(changeSub)
								strcat(pre, "\\sub");
						}
					}
					
					if(pre[0]) {
						strcat(pre, " ");
												
						t->pre = pre;
						t->preSize = strlen(pre);
						
						mPageLength += t->preSize;
					}
					else
						free(pre);
				}
				
				if(post) {
					if(mSuperSubChanges && (changeSuper || changeSub))
						strcat(post, "\\nosupersub");
					
					if(post[0]) {	
						strcat(post, " {}");
						
						t->post = post;
						t->postSize = strlen(post);
						
						mPageLength += t->postSize;
					}
					else
						free(post);
				}
			}
		}
	}
}

void
CPDFParser::ObjectsToHTML()
{
	std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
	PDFTextObject* t;

	PDFFontObject* currentFont = NULL;
	long currentSize = 1;
	
	bool boldStyle = false;
	bool italicStyle = false;
	bool inFont = false;
	
	for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
		t = *i;

		if(t->text && t->size && t->ws == false) {
			long escapeBytes = 0;
			
			for(long j = 0; j < t->size; j++) {
				if(t->text[j] > 127)
					escapeBytes += 5;
				else switch(t->text[j]) {
					case '&':
						escapeBytes++;
						
					case '<':
					case '>':
						escapeBytes += 3;
				}
			}
			
			if(escapeBytes) {
				unsigned char* escapedText = (unsigned char*) malloc(t->size + escapeBytes);
				if(escapedText) {
					long ei = 0;
					
					for(long j = 0; j < t->size; j++) {
						if(t->text[j] > 127) {
							char escaped[8];
							sprintf(escaped, "&#%d;", t->text[j]);
							long len = strlen(escaped);
							for(long k = 0; k < len; k++)
								escapedText[ei++] = escaped[k];
						}
						else switch(t->text[j]) {
							case '&':
								escapedText[ei++] = '&';
								escapedText[ei++] = 'a';
								escapedText[ei++] = 'm';
								escapedText[ei++] = 'p';
								escapedText[ei++] = ';';
								break;
								
							case '<':
								escapedText[ei++] = '&';
								escapedText[ei++] = 'l';
								escapedText[ei++] = 't';
								escapedText[ei++] = ';';
								break;
								
							case '>':
								escapedText[ei++] = '&';
								escapedText[ei++] = 'g';
								escapedText[ei++] = 't';
								escapedText[ei++] = ';';
								break;
								
							default:
								escapedText[ei++] = t->text[j];
						}
					}
					
					free(t->text);
					t->text = escapedText;
					t->size += escapeBytes;
				}
			}
						
			bool changeFont = false;
			bool changeSize = false;
			bool changeBold = false;
			bool changeItalic = false;
			
			if(t->font) {
				if(t->font != currentFont) {
					changeFont = true;
					currentFont = t->font;
				}
				
				changeBold = t->font->bold;
				changeItalic = t->font->italic;
			}
			
			bool changeSuper = false;
			bool changeSub = false;
			
			if(fequal_(t->ty, t->y) == false) {
				if(t->ty > t->y)
					changeSuper = true;
				else if(t->ty < t->y)
					changeSub = true;
			}
			
			long fontSize = lroundf(t->f);
							
			if(fontSize <= 8)
				fontSize = 1;
			else if(fontSize <= 10)
				fontSize = 2;
			else if(fontSize <= 12)
				fontSize = 3;
			else if(fontSize <= 14)
				fontSize = 4;
			else if(fontSize <= 18)
				fontSize = 5;
			else if(fontSize <= 24)
				fontSize = 6;
			else
				fontSize = 7;
													
			if(fontSize != currentSize) {
				changeSize = true;
				currentSize = fontSize;
			}
			
			if(changeFont || changeSize || changeSuper || changeSub) {
				char* pre = (char*) malloc(64);
				pre[0] = 0;

				char* post = NULL;
				if((changeSuper || changeSub) && pre) {
					post = (char*) malloc(64);
					post[0] = 0;
				}
				
				if(pre) {
					char buf[64];
					
					if(mStyleChanges) {
						if(changeBold == false && boldStyle) {
							strcat(pre, "</b>");
							boldStyle = false;
						}
					
						if(changeItalic == false && italicStyle) {
							strcat(pre, "</i>");
							italicStyle = false;
						}
					}
										
					/*if(mFontChanges && changeFont) {
						sprintf(buf, "\\f%d", t->font->index);
						strcat(pre, buf);
					}*/
						
					if((mFontChanges && changeFont) || (mSizeChanges && changeSize)) {
						if(currentFont) {				
							if(inFont)
								sprintf(buf, "</font><font face=\"%s\" size=%d>", currentFont->baseFont, (int)currentSize);
							else {
								sprintf(buf, "<font face=\"%s\" size=%d>", currentFont->baseFont, (int)currentSize);
								inFont = true;
							}
						}
						else {
							if(inFont)
								sprintf(buf, "</font><font size=%d>", (int)currentSize);
							else {
								sprintf(buf, "<font size=%d>", (int)currentSize);
								inFont = true;
							}
						}
						strcat(pre, buf);
					}
					
					if(mStyleChanges) {
						if(changeBold && boldStyle == false) {
							strcat(pre, "<b>");
							boldStyle = true;
						}
					
						if(changeItalic && italicStyle == false) {
							strcat(pre, "<i>");
							italicStyle = true;
						}
					}
						
					if(post) {
						if(mSuperSubChanges) {
							if(changeSuper)
								strcat(pre, "<sup>");
							else if(changeSub)
								strcat(pre, "<sub>");
						}
					}
					
					if(pre[0]) {
						t->pre = pre;
						t->preSize = strlen(pre);
						
						mPageLength += t->preSize;
					}
					else
						free(pre);
				}
				
				if(post) {
						if(mSuperSubChanges) {
							if(changeSuper)
								strcat(post, "</sup>");
							else if(changeSub)
								strcat(post, "</sub>");
						}
					
					if(post[0]) {	
						t->post = post;
						t->postSize = strlen(post);
						
						mPageLength += t->postSize;
					}
					else
						free(post);
				}
			}
		}
	}
	
	mPageFont = inFont;
	mPageBold = boldStyle;
	mPageItalic = italicStyle;
}

bool
CPDFParser::Strip()
{
	if(mData == NULL || mDataSize == 0)
		return false;

	unsigned char* buffer = (unsigned char*) malloc(mDataSize);
	
	if(buffer) {
		// strip whitespace
		long bufferSize = 0;
		bool newline = false;
		
		// ...from the top of the page
		long i = 0;
		while(isspace(mData[i]) && i < mDataSize)
			i++;
					
		for(; i < mDataSize; i++) {
			if(newline) {
				// ...from the beginning of a line
				while(mData[i] == ' ' && i < mDataSize)
					i++;
					
				newline = false;
			}
			else if(mData[i] == ' ') {
				// ...doubled up inside a line
				if(bufferSize && buffer[bufferSize - 1] == ' '	&& buffer[bufferSize - 2] == ' ')
					continue;	
			}
			
			if(mData[i] == '\n') {
				newline = true;
				
				// ...from the end of a line
				while(bufferSize && buffer[bufferSize - 1] == ' ')
					bufferSize--;
					
				// ...doubled up line spacing
				if(bufferSize > 1 && buffer[bufferSize - 1] == '\n'	&& buffer[bufferSize - 2] == '\n')
					continue;	
			}
			
			buffer[bufferSize++] = mData[i];
		}
		
		// ...from the end of the page
		while(bufferSize > 1 && buffer[bufferSize - 1] == '\n' && buffer[bufferSize - 2] == '\n')
			bufferSize--;
		
		free(mData);
		mData = buffer;
		mDataSize = bufferSize;
		
		return true;
	}
	
	return false;
}

bool
CPDFParser::Clean()
{
	if(mData == NULL || mDataSize == 0)
		return false;

	unsigned char* buffer = (unsigned char*) malloc(mDataSize);
	
	if(buffer) {
		// strip whitespace
		long bufferSize = 0;
		
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n') {
				// ... from the end of a line
				while(bufferSize && buffer[bufferSize - 1] == ' ')
					bufferSize--;					
			}
			
			buffer[bufferSize++] = mData[i];
		}
				
		free(mData);
		mData = buffer;
		mDataSize = bufferSize;
		
		return true;
	}
	
	return false;
}

bool
CPDFParser::Rewrap()
{
	if(mData == NULL || mDataSize == 0)
		return false;

	unsigned char* buffer = (unsigned char*) malloc(mDataSize);
	
	if(buffer) {
		long bufferSize = 0;
		long wrap = 0;
		
		for(long i = 0; i < mDataSize; i++) {
			if(wrap && i < wrap) {
				if(mData[i] == '\n') {
					buffer[bufferSize++] = ' ';
					continue;
				}
			}
			else {
				bool feed = false;
	
				if(i < 2 || (mData[i - 1] == '\n' && mData[i - 2] == '\n')) {
					feed = true;
					
					if(mType >= kWriteRTF) {
						if(mData[i] == '\\' && !istoken_(mData[i + 1])) {
							if(i && mData[i - 1] == '\\')
								;
							else {
								while(i < mDataSize && mData[i] != ' ')
									buffer[bufferSize++] = mData[i++];

								if(i < mDataSize)
									buffer[bufferSize++] = mData[i++];
							}
						}
					}
				}
								
				if(isupper(mData[i]) || mData[i] == 0xD2 || mData[i] == 0xD4) {
					if(feed) {
						bool nl = false;
						
						wrap = 0;
						
						long j = i;
						while(j < mDataSize && wrap == 0) {
							if(mData[j] == '\n' && mData[j - 1] == '\n') {
								bool good = false;
								long k = j - 2;
								
								bool foundText = false;
								
								while(k > i && mData[k] != '\n') {
									bool doBreak = true;
									
									switch(mData[k]) {
										case '.':
											if(mData[k - 1] != '.')
												good = true;
											break;
											
										case '?':
										case '!':
										case ':':
										//case 'É':
											good = true;
											break;		
											
										default:
											doBreak = false;
									}
									
									if(isalpha(mData[k]))
										foundText = true;
										
									if(doBreak || mData[k] == ',' || mData[k] == ';')
										break;
									
									k--;
								}
								
								if(foundText) {
									if(mType >= kWriteRTF) {
										for(long z = k; z < j - 3; z++) {
											if(mData[z] == '\\') {
												if(istoken_(mData[z + 1]))
													z++;
												else
													break;
											}
												
											if(isalpha(mData[z]))
												good = false;
										}
									}
									else
										good = false;
								}
																
								if(good)
									wrap = j - 2;
								else
									break;
							}
							else if(mData[j] == '\n')
								nl = true;
							else if(mData[j] == '\t')
								break;
							else {
								if(nl) {
									if(mType >= kWriteRTF && mData[j] == '\\' && j && mData[j - 1] != '\\' && !istoken_(mData[j + 1])) {
										while(j < mDataSize && mData[j] != ' ')
											j++;
									}
									else if(isalnum(mData[j]))
										nl = false;
									else if(isspace(mData[j]))
										break;
								}
							}
							
							j++;
						}
					}
				}
			}
						
			buffer[bufferSize++] = mData[i];
		}
	
		free(mData);
		mData = buffer;
		mDataSize = bufferSize;
		
		return true;
	}
	
	return false;
}

void
CPDFParser::PageToRTF()
{
	if(mData == NULL || mDataSize == 0)
		return;

	long nlCount = 0;
	long indent = 0;
	long depth = 0;
	long wsStrip = 0;
	
	if(mPadStrip) {
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n')
				nlCount++;
		}
	}
	else {
		while(depth < mDataSize && mData[depth] == '\n')
			depth++;
	
		indent = mCol;
	
		bool feed = false;
		long wsCount = 0;
		
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n') {
				nlCount++;
				
				feed = true;
				wsCount = 0;
			}
			else if(feed) {
				if(mData[i] == ' ')
					wsCount++;
				else if(mData[i] == '{' && i && mData[i - 1] != '\\') {
					// ignore rtf codes
					while(i < mDataSize && mData[i] != '}')
						i++;
				}
				else if(mData[i] == '\\' && i && mData[i - 1] != '\\' && !istoken_(mData[i + 1])) {
					// ignore rtf codes
					while(i < mDataSize && mData[i] != ' ')
						i++;
				}
				else {
					if(wsCount && wsCount < indent)
						indent = wsCount;
						
					wsStrip += wsCount;
						
					feed = false;
				}
			}
		}
		
		if(mType == kWriteRTF) {
			depth = 0;
			indent = 0;
		}
		else {	
			mTopMargin = lroundf((float) depth / ys);
			if(mTopMargin > 108) {
				mTopMargin = 108;
				depth = lroundf(ys * 108.0);
			}
			
			mLeftMargin = lroundf((float) indent / xs);
			if(mLeftMargin > 108) {
				mLeftMargin = 108;
				indent = lroundf(xs * 108.0);
			}
		}
	}
		
	long margin = 0;
	long marginSet = 0;
	
	unsigned char* buffer = (unsigned char*) malloc(mDataSize + (nlCount * 16));
	
	long lineCount = 0;
	
	if(buffer) {
		long bufferSize = 0;
		bool feed = false;
			
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n') {
				if(i >= depth) {
					buffer[bufferSize++] = '\\';
					buffer[bufferSize++] = '\n';
				}
				
				feed = true;
				margin = 0;
			
				lineCount++;
			}
			else if(mData[i] == '\\' && i && mData[i - 1] != '\\' && !istoken_(mData[i + 1])) {
				// ignore rtf codes
				while(i < mDataSize && mData[i] != ' ') {
					buffer[bufferSize++] = mData[i];
					i++;
				}
				
				if(i < mDataSize)
					buffer[bufferSize++] = mData[i];
			}
			else {
				if(mData[i] == ' ' && feed)
					margin++;
				else {
					if(feed) {
						if(margin) {
							margin -= indent;
							
							if(margin != marginSet) {
								if(margin > 0) {
									char marginEntry[256];
									sprintf(marginEntry, "\\li%d\n", (int)(20 * lroundf((float) margin / xs)));
										
									long bytes = strlen(marginEntry);
									memcpy(&(buffer[bufferSize]), marginEntry, bytes);
									bufferSize += bytes;
								}
								else {
									char marginEntry[256] = "\\li0\n";
										
									long bytes = strlen(marginEntry);
									memcpy(&(buffer[bufferSize]), marginEntry, bytes);
									bufferSize += bytes;
									
									margin = 0;
								}
										
								marginSet = margin;
							}
														
							margin = 0;				
						}
						else if(marginSet) {
							char marginEntry[256] = "\\li0\n";
								
							long bytes = strlen(marginEntry);
							memcpy(&(buffer[bufferSize]), marginEntry, bytes);
							bufferSize += bytes;
							
							marginSet = 0;
						}
						
						feed = false;
					}

					if(mData[i] == '{' && i && mData[i - 1] != '\\') {
						// ignore rtf codes
						while(i < mDataSize && mData[i] != '}') {
							buffer[bufferSize++] = mData[i];
							i++;
						}	
						
						if(i < mDataSize)
							buffer[bufferSize++] = mData[i];
					}
					else										
						buffer[bufferSize++] = mData[i];

					feed = false;
				}
			}
		}
		
		free(mData);
		mData = buffer;
		mDataSize = bufferSize;
	}
}

void
CPDFParser::PageToHTML()
{
	if(mData == NULL || mDataSize == 0)
		return;

	long nlCount = 0;
	long wsTotal = 0;
	
	long indent = 0;
	long depth = 0;
	long wsStrip = 0;
	
	if(mPadStrip) {
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n')
				nlCount++;
		}
	}
	else {
		while(depth < mDataSize && mData[depth] == '\n')
			depth++;
	
		indent = mCol;
	
		bool feed = false;
		long wsCount = 0;
		
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n') {
				nlCount++;
				
				feed = true;
				
				wsTotal += wsCount;
				wsCount = 0;
			}
			else if(feed) {
				if(mData[i] == ' ')
					wsCount++;
				else if(mData[i] == '<') {
					// ignore html codes
					while(i < mDataSize && mData[i] != '>')
						i++;
				}
				else {
					if(wsCount && wsCount < indent)
						indent = wsCount;
						
					wsStrip += wsCount;
						
					feed = false;
				}
			}
			else if(mData[i] == ' ' && i + 1 < mDataSize && mData[i + 1] == ' ')
				wsTotal++;
		}
		
		depth = 0;
		
		/*mTopMargin = lroundf((float) depth / ys);
		if(mTopMargin > 108) {
			mTopMargin = 108;
			depth = lroundf(ys * 108.0);
		}*/
		
		mLeftMargin = lroundf((float) indent / xs);
		if(mLeftMargin > 108) {
			mLeftMargin = 108;
			indent = lroundf(xs * 108.0);
		}
	}
		
	long margin = 0;
	//long marginSet = 0;

	unsigned char* buffer = (unsigned char*) malloc(mDataSize + (nlCount * 16) + (wsTotal * 6) + 16);
		
	if(buffer) {
		long bufferSize = 0;
		bool feed = true;
			
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n') {
				if(i >= depth) {
					buffer[bufferSize++] = '<';
					buffer[bufferSize++] = 'b';
					buffer[bufferSize++] = 'r';
					buffer[bufferSize++] = '>';
					buffer[bufferSize++] = '\n';
					
					feed = true;
					margin = 0;
				}
			}
			else {
				if(mData[i] == ' ' && feed) {
					margin++;
				
					if(mPadStrip == false && margin > indent) {
						buffer[bufferSize++] = '&';
						buffer[bufferSize++] = 'n';
						buffer[bufferSize++] = 'b';
						buffer[bufferSize++] = 's';
						buffer[bufferSize++] = 'p';
						buffer[bufferSize++] = ';';
					}
				}	
				else {
					if(feed) {
						if(margin) {
							margin -= indent;
							
							if(margin > 0) {
							}
														
							margin = 0;				
						}
						
						feed = false;
					}

					if(mData[i] == '<') {
						// ignore html codes
						while(i < mDataSize && mData[i] != '>') {
							buffer[bufferSize++] = mData[i];
							i++;
						}	
						
						if(i < mDataSize)
							buffer[bufferSize++] = mData[i];
					}
					else if(mData[i] == ' ' && i + 1 < mDataSize && mData[i + 1] == ' ') {
						buffer[bufferSize++] = '&';
						buffer[bufferSize++] = 'n';
						buffer[bufferSize++] = 'b';
						buffer[bufferSize++] = 's';
						buffer[bufferSize++] = 'p';
						buffer[bufferSize++] = ';';
					}
					else										
						buffer[bufferSize++] = mData[i];

					feed = false;
				}
			}
		}
		
		if(mType == kWriteHTML) {
			if(mPageFont) {
				buffer[bufferSize++] = '<';
				buffer[bufferSize++] = '/';
				buffer[bufferSize++] = 'f';
				buffer[bufferSize++] = 'o';
				buffer[bufferSize++] = 'n';
				buffer[bufferSize++] = 't';
				buffer[bufferSize++] = '>';
			}
			
			if(mPageBold) {
				buffer[bufferSize++] = '<';
				buffer[bufferSize++] = '/';
				buffer[bufferSize++] = 'b';
				buffer[bufferSize++] = '>';
			}
			
			if(mPageItalic) {
				buffer[bufferSize++] = '<';
				buffer[bufferSize++] = '/';
				buffer[bufferSize++] = 'i';
				buffer[bufferSize++] = '>';
			}
		}
				
		free(mData);
		mData = buffer;
		mDataSize = bufferSize;
	}
}

void
CPDFParser::FixNewlines()
{
	if(mData == NULL || mDataSize == 0)
		return;

	if(mNewlineCode == kNewlineUNIX)
		return;
		
	if(mNewlineCode == kNewlineMac) {
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n')
				mData[i] = '\r';
		}
	}
	else if(mNewlineCode == kNewlineDOS) {
		long nlCount = 0;
		
		for(long i = 0; i < mDataSize; i++) {
			if(mData[i] == '\n')
				nlCount++;
		}
		
		if(nlCount) {
			unsigned char* buffer = (unsigned char*) malloc(mDataSize + nlCount);
				
			if(buffer) {
				long bufferSize = 0;
					
				for(long i = 0; i < mDataSize; i++) {
					if(mData[i] == '\n') {
						buffer[bufferSize++] = '\r';
						buffer[bufferSize++] = '\n';
					}
					else
						buffer[bufferSize++] = mData[i];
				}

				free(mData);
				mData = buffer;
				mDataSize = bufferSize;
			}
		}
	}
}

#pragma mark -

unsigned char*
CPDFParser::Extract(
	const unsigned char* p,
	long n)
{
	unsigned char* outChunk = (unsigned char*) malloc(n);
	long chunkIndex = 0;
	
	if(outChunk == NULL)
		return NULL;
		
	bool insideString = false;
	
	for(long j = 0; j < n; j++) {
		if(p[j] == '(')
			insideString = true;
		else if(p[j] == ')')
			insideString = false;
		else if(insideString) {
			if(p[j] == '\\')
				j++;
				
			outChunk[chunkIndex++] = p[j];
		}
	}
	
	if(cd)
		outChunk[chunkIndex] = 0;
	else {
		free(outChunk);
		outChunk = NULL;
	}
	
	return outChunk;
}

void
CPDFParser::Store()
{
	mParseIntoStore = true;
}

void
CPDFParser::Release()
{
	if(mParseIntoStore) {
		mParseIntoStore = false;

		std::vector<StoreObject*>::const_iterator i = mStore.begin();
		StoreObject* object;
			
		for(i = mStore.begin(); i != mStore.end(); i++) {
			object = *i;
			
			Parse(object->data, object->dataSize, (i == mStore.begin() ? true : false));
			
			free(object->data);
			free(object);
		}
		
		mStore.clear();
	}
}

void
CPDFParser::Parse(
	const unsigned char* p,
	long n,
	bool init)
{
	if(mParseIntoStore && n) {
		unsigned char* pc = (unsigned char*) malloc(n);
		if(pc) {
			StoreObject* object = (StoreObject*) malloc(sizeof(StoreObject));
			if(object) {
				memcpy(pc, p, n);
				object->data = pc;
				object->dataSize = n;
				
				mStore.push_back(object);
			
				return;
			}
			else
				free(pc);
		}
	}
	
	mParseBytes += n;
	
	if(init) {
		insideString = false;
		insideHex = false;
		insideArray = false;
		insideText = false;
		insideImage = false;
	}
		
	for(long j = 0; j < n; j++) {
		if(insideText && p[j] == '(') {
			insideString = true;
			
			if(insideArray) {
				float jFloor = (mRelaxSpacing ? -1.0 : -500.0);
				
				if(cd && mArrayJ < jFloor) {
					float delta = -mArrayJ;
										
					float space = 1000.0;
					float threshold = 0.0;
					bool fit = false;
					
					if(mFont && mFont->widths) {
						space = GetCharacterWidth(' ');
						threshold = (space * 4.0);
						fit = true;
					}
					
					if(fit && delta > threshold) {
						float cw = GetTextWidth();
						
						float saveY = mY;
						mY += mF * mS;
						ProcessChunk();
						mY = saveY;

						mX -= mF * ((mArrayJ - cw) / 1000.0);
					}
					else {
						long spacing = lroundf(delta / space);
						
						if(mRelaxSpacing && spacing == 0)
							spacing = 1;
							
						while(spacing) {
							c[cd++] = ' ';
							spacing--;
						}
					}
				}
			}
		}
		else if(insideString && p[j] == ')')
			insideString = false;
		else if(insideString) {
			OpenChunker(n);
			
			unsigned char charInStream = 0;
			
			if(j < (n - 1) && p[j] == '\\') {
				j++;
				
				if(isdigit(p[j])) {
					char octal[4];
					long od = 0;
					
					while(od < 3 && j < n && p[j] >= '0' && p[j] <= '8')
						octal[od++] = p[j++];
						
					j--;
					
					if(od) {
						octal[od] = 0;
					
						long swap = (strtol(octal, (char**) NULL, 8));
                        long charInOctal = swap;
						
						if(charInOctal >= 0 && charInOctal <= 255) {
							unsigned char charInChar = (unsigned char) charInOctal;
							charInStream = (mFont && mFont->mapInPlace ? mFont->map[charInChar] : charInChar);							
						}
					}
				}
				else {
					switch(p[j]) {
						case 'n':
							c[cd++] = '\n';
							break;
							
						case 'r':
							c[cd++] = '\r';
							break;
							
						case 't':
							c[cd++] = '\t';
							break;
							
						case 'b':
							c[cd++] = '\b';
							break;
							
						case 'f':
							c[cd++] = '\f';
							break;
							
						/*case '(':
						case ')':
							charInStream = (mFont && mFont->mapInPlace ? mFont->map[p[j]] : p[j]);
							break;
							
						case '\\':	
							charInStream = (mFont && mFont->mapInPlace ? mFont->map[p[j]] : p[j]);
							break;*/
							
						default:
							charInStream = (mFont && mFont->mapInPlace ? mFont->map[p[j]] : p[j]);
					}
				}
			}
			else
				charInStream = (mFont && mFont->mapInPlace ? mFont->map[p[j]] : p[j]);
			
			if(charInStream) {				
				if(mType >= kWriteRTF) {
					switch(charInStream) {
						case '\\':
						case '{':
						case '}':
							c[cd++] = '\\';
					}
				}
				
				c[cd++] = charInStream;
			}
		}
		else if(insideText && p[j] == '<') {
			OpenChunker(n);
			
			mHexStart = cd;
			
			insideHex = true;
		}
		else if(insideHex && p[j] == '>') {
			insideHex = false;
			
			long hexLength = cd - mHexStart;
			if(hexLength) {
				long hexEnd = cd;
				cd = mHexStart;
									
				for(long i = mHexStart; i < hexEnd; i++) {
					char hex[5] = "0000";
					
					hex[0] = c[i];
							
					if(++i < hexEnd)
						hex[1] = c[i];

					hex[2] = 0;
						
					//if(++i < hexEnd) // if not 8 byte hex
					//	hex[2] = c[i];	

					//if(++i < hexEnd)
					//	hex[3] = c[i];
												
					long swap = (strtol(hex, (char**) NULL, 16));
                    long charInHex = swap;
					
					if(charInHex >= 0 && charInHex <= 255) {
						unsigned char charInChar = (unsigned char) charInHex;
						unsigned char charInStream = (mFont && mFont->mapInPlace ? mFont->map[charInChar] : charInChar);
						
						if(charInStream) {				
							if(mType >= kWriteRTF) {
								switch(charInStream) {
									case '\\':
									case '{':
									case '}':
										c[cd++] = '\\';
								}
							}
							
							c[cd++] = charInStream;
						}
					}
					else {
						/*//c[cd++] = MapUnicode(charInHex);
						if(mType >= kWriteRTF) {
							char us[16];
							sprintf(us, "{\\u%d?}", charInHex);
							long ul = strlen(us);
							for(long i = 0; i < ul; i++)
								c[cd++] = us[i];
						}*/
					}
					
				}
			}
		}
		else if(insideHex) {
			if(isdigit(p[j]) || (p[j] >= 'A' && p[j] <= 'F'))
				c[cd++] = p[j];
		}
		else if(insideImage) {
			if(p[j - 1] == 'E' && p[j] == 'I') {
				insideImage = false;
			}
		}
		else if(j) {
			if(p[j] == '[') {
				insideArray = true;
				mArrayJ = 0.0;
				
				mArrayX = mX;
			}
			else if(p[j] == ']')
				insideArray = false;
			
			char operand[256];
			long oo = 0;
			
			while(j < n && (isdigit(p[j]) || p[j] == '-' || p[j] == '.')) {
				operand[oo++] = p[j];
				j++;
			}
			
			if(oo) {
				for(long i = kMaxOperands - 1; i > 0; i--)
					mOperand[i] = mOperand[i - 1];
					
				operand[oo] = 0;
				
				CFSwappedFloat32 swap = CFConvertFloat32HostToSwapped(strtof(operand, (char**) NULL));
				mOperand[0] = CFConvertFloat32SwappedToHost(swap);
				
				if(p[j] == '(' && insideArray)
					mArrayJ = mOperand[0];
					
				j--;
			}
			else {
				if(p[j] == '\'' || p[j] == '\"') {
					mY -= mF * mL;
					
					if(cd) {
						float saveY = mY;
						mY += mF * mS;
						ProcessChunk();
						mY = saveY;
					}
				}
				else if(p[j] == 'q') {
					if(mQDepth < kMaxQDepth) {
						mSaveScale[mQDepth] = mScale;
						mSaveDeltaX[mQDepth] = mDeltaX;
						mSaveDeltaY[mQDepth] = mDeltaY;
						
						mQDepth++;
					}
				}	
				else if(p[j] == 'Q') {
					if(mQDepth) {
						mQDepth--;
						
						mScale = mSaveScale[mQDepth];
						mDeltaX = mSaveDeltaX[mQDepth];
						mDeltaY = mSaveDeltaY[mQDepth];
					}
				}	
				else if(p[j - 1] == 'c' && p[j] == 'm') {
					mScale *= mOperand[5];
					mDeltaX += mOperand[1];
					mDeltaY += mOperand[0];
				}
				else if(p[j - 1] == 'D' && p[j] == 'o' && init == true) {
					long k = j;
					while(k && p[k] != '/')
						k--;
						
					if(p[k] == '/' && ++k < j) {
						char* objName = (char*) malloc(j - k);
						if(objName) {
							long objLength = 0;
							
							while(k < j && isspace(p[k]) == false)
								objName[objLength++] = p[k++];

							objName[objLength] = 0;
							
							if(objLength) {
								PDFXObject* object = GetXObject(objName);
								if(object) {
									if(mQDepth < kMaxQDepth) {
										mSaveScale[mQDepth] = mScale;
										mSaveDeltaX[mQDepth] = mDeltaX;
										mSaveDeltaY[mQDepth] = mDeltaY;
										
										mQDepth++;
									}

									Parse(object->data, object->size, false);

									if(mQDepth) {
										mQDepth--;
										
										mScale = mSaveScale[mQDepth];
										mDeltaX = mSaveDeltaX[mQDepth];
										mDeltaY = mSaveDeltaY[mQDepth];
									}
								}
							}
							
							free(objName);
						}
					}					
				}
				else if(p[j - 1] == 'B' && p[j] == 'I') {
					insideImage = true;
				}
				else if(p[j - 1] == 'B' && p[j] == 'T') {
					InitMetrics();
					insideText = true;
				}
				else if(p[j - 1] == 'E' && p[j] == 'T') {
					insideText = false;
				}
				else if(p[j - 1] == 'T') {
					switch(p[j]) {
						case 'f':
						{
							mFS = mOperand[0];
							
							long k = j;
							while(k && p[k] != '/')
								k--;
								
							if(p[k] == '/' && ++k < j) {
								char* fontName = (char*) malloc(j - k);
								if(fontName) {
									long fontLength = 0;
									
									while(k < j && isspace(p[k]) == false)
										fontName[fontLength++] = p[k++];

									fontName[fontLength] = 0;
									
									if(fontLength) {
										if(mFont == NULL || strcmp(mFont->key, fontName) != 0)
											mFont = GetFont(fontName);
									}
									
									free(fontName);
								}
							}
							
							break;
						}
							
						case 'c':
							mTC = mOperand[0];
							break;
							
						case 'w':
							mTW = mOperand[0];
							break;
							
						case 'L':
							mL = mOperand[0];
							break;
								
						case 's':
							mS = mOperand[0];
							break;		
														
						case 'm':
						{
							mF = mScale * mOperand[2];
							mX = mScale * (mDeltaX + mOperand[1]);
							
							if(mF < 0.0) {
								mY = mDeltaY - (mScale * mOperand[0]);
								mF *= -1.0;
							}
							else
								mY = mDeltaY + (mScale * mOperand[0]);
								
							mLX = mX;
							break;
						}
							
						case 'D':
							mL = -mOperand[0];
							
						case 'd':
							if(fequal_(mLX, 0.0))
								mLX = mX + (mScale * mDeltaX);
								
							mX = mLX + (mF * mOperand[1]);
							mY += (mF * mOperand[0]);
							
							mLX = mX;
							
							//mX += (mF * mOperand[1]);
							//mY += (mF * mOperand[0]);
							break;
							
						case '*':
							if(fequal_(mL, 0.0))
								mY -= mF;
							else
								mY -= mF * mL;
							break;
							
						case 'J':	
							if(cd) {
								float saveY = mY;
								mY += mF * mS;
								ProcessChunk();
								mY = saveY;
							}
							
							mX = mArrayX;
							break;
							
						case 'j':
							if(cd) {
								float saveY = mY;
								mY += mF * mS;
								ProcessChunk();
								mY = saveY;
							}
							break;
					}
				}
			}
		}
	}
}

#pragma mark -

void
CPDFParser::AddXObject(
	const char* inKey,
	const unsigned char* inData,
	long inSize)
{
	PDFXObject* object = (PDFXObject*) malloc(sizeof(PDFXObject));
	
	if(object) {
		object->key = (char*) malloc(strlen(inKey) + 1);

		if(object->key) {
			strcpy(object->key, inKey);

			object->data = (unsigned char*) malloc(inSize);
			if(object->data) {
				memcpy(object->data, inData, inSize);
				
				object->size = inSize;
				
				mObjectTable.push_back(object);
				return;
			}
		}

		if(object->key)
			free(object->key);

		free(object);
	}
}

CPDFParser::PDFXObject*
CPDFParser::GetXObject(
	const char* inKey)
{
	std::vector<PDFXObject*>::const_iterator i = mObjectTable.begin();
	PDFXObject* x;
	
	for(i = mObjectTable.begin(); i != mObjectTable.end(); i++) {
		x = *i;
		
		if(strcmp(inKey, x->key) == 0)
			return x;
	}
	
	return NULL;
}

void
CPDFParser::FreeXObjects()
{
	std::vector<PDFXObject*>::const_iterator i = mObjectTable.begin();
	PDFXObject* x;
	
	for(i = mObjectTable.begin(); i != mObjectTable.end(); i++) {
		x = *i;
		
		free(x->data);
		free(x->key);

		free(x);
	}
		
	mObjectTable.clear();
}

void
CPDFParser::AddFont(
	const char* inKey,
	const char* inBaseFont,
	const char* inEncoding,
	char* inMap,
	wchar_t* inUMap,
	float* inWidths,
	unsigned char inFI,
	unsigned char inFL)
{
	PDFFontObject* font = (PDFFontObject*) malloc(sizeof(PDFFontObject));
	
	if(font) {
		long baseFontSize = strlen(inBaseFont);
		bool subset = false;
		
		if(baseFontSize > 7 && inBaseFont[6] == '+') {
			baseFontSize -= 7;
			subset = true;
		}
			
		font->key = (char*) malloc(strlen(inKey) + 1);
		font->baseFont = (char*) malloc(baseFontSize + 1);
		font->family = (char*) malloc(8);
		font->map = inMap;
		font->umap = inUMap;
		font->widths = inWidths;
			
		if(font->key && font->baseFont) {
			strcpy(font->key, inKey);
			
			if(subset)
				strcpy(font->baseFont, &(inBaseFont[7]));
			else
				strcpy(font->baseFont, inBaseFont);
			
			TextEncoding encoding = kEncodeMacRoman;
			
			if(inEncoding == NULL || strcmp(inEncoding, "MacRomanEncoding") == 0)
				encoding = kEncodeMacRoman;
			else if(strcmp(inEncoding, "WinAnsiEncoding") == 0)
				encoding = kEncodeWinANSI;
			else if(strcmp(inEncoding, "MacExpertEncoding") == 0)
				encoding = kEncodeMacExpert;
			else if(strcmp(inEncoding, "MacSymbolEncoding") == 0)
				encoding = kEncodeMacSymbol;
			else if(strcmp(inEncoding, "MacDingbatEncoding") == 0)
				encoding = kEncodeMacDingbats;

			font->encoder = AddEncoder(encoding);

			font->bold = false;
			font->italic = false;
			
			font->fi = inFI;
			font->fl = inFL;

			if(font->map && font->umap == NULL && font->fi == 0 && font->fl == 0)
				font->mapInPlace = true;
			else
				font->mapInPlace = false;
			
			char* synth = strchr(font->baseFont, '-');
			
			if(synth == NULL)
				synth = strchr(font->baseFont, ',');
			
			if(synth) {
				if(strstr(font->baseFont, "-BoldItal") || strstr(font->baseFont, ",BoldItal") ) { // matches -BoldItal(ic)
					font->bold = true;
					font->italic = true;
				}
				else if(strstr(font->baseFont, "-Bold") || strstr(font->baseFont, ",Bold"))
					font->bold = true;
				else if(strstr(font->baseFont, "-Ital") || strstr(font->baseFont, ",Ital")) // matches -Ital(ic)
					font->italic = true;
					
				if(mType == kWriteRTFWord || mType == kWriteHTML)
					*synth = 0;
			}
			
			if(font->bold == false &&
				(
					strstr(font->baseFont, "Bold") ||
					strstr(font->baseFont, "BdMS") ||
					strstr(font->baseFont, "BdItMS")
				)
			)
				font->bold = true;
				
			if(font->italic == false &&
				(
					strstr(font->baseFont, "Oblique") ||
					strstr(font->baseFont, "Italic") ||
					strstr(font->baseFont, "ItMS")
				)
			)
				font->italic = true;
				
			if(mType == kWriteRTFWord || mType == kWriteHTML) {
				long len = strlen(font->baseFont);
				
				if(len > 6 && strcmp(&(font->baseFont[len - 6]), "BdItMS") == 0)
					font->baseFont[len - 6] = 0;						
				else if
				(
					len > 4 &&
					(
						strcmp(&(font->baseFont[len - 4]), "BdMS") == 0 ||
						strcmp(&(font->baseFont[len - 4]), "ItMS") == 0 ||
						strcmp(&(font->baseFont[len - 4]), "PSMT") == 0
					)
				)
					font->baseFont[len - 4] = 0;
				else if
				(
					len > 3 &&
					(
						strcmp(&(font->baseFont[len - 3]), "ITC") == 0
					)
				)
					font->baseFont[len - 3] = 0;
				else if
				(
					len > 2 &&
					(
						strcmp(&(font->baseFont[len - 2]), "PS") == 0 ||
						strcmp(&(font->baseFont[len - 2]), "MT") == 0 ||
						strcmp(&(font->baseFont[len - 2]), "MS") == 0
					)
				)
					font->baseFont[len - 2] = 0;

				// respace mixed caps
				len = strlen(font->baseFont);
				
				long cc = 0;
				for(long i = 0; i < len - 1; i++) {
					if(islower(font->baseFont[i]) && isupper(font->baseFont[i + 1]))
						cc++;
				}
				
				if(cc) {
					char* mixed = (char*) malloc(len + cc + 1);
					
					if(mixed) {
						long ml = 0;
						
						for(long i = 0; i < len; i++) {
							mixed[ml++] = font->baseFont[i];
							
							if(
								i < len - 2 &&
								islower(font->baseFont[i]) &&
								isupper(font->baseFont[i + 1]) &&
								islower(font->baseFont[i + 2])
							)
								mixed[ml++] = ' ';
						}
						
						mixed[ml] = 0;
						
						free(font->baseFont);
						font->baseFont = mixed;
					}				
				}
			}
						
			if(font->family) {
				if(strstr(font->baseFont, "Times") || strstr(font->baseFont, "Palatino"))
					strcpy(font->family, "roman");
				else if(strstr(font->baseFont, "Helvetica") || strstr(font->baseFont, "Geneva") || strstr(font->baseFont, "Arial"))
					strcpy(font->family, "swiss");
				else if(strstr(font->baseFont, "Courier") || strstr(font->baseFont, "Monaco"))
					strcpy(font->family, "modern");
				else if(strstr(font->baseFont, "Cursive") || strstr(font->baseFont, "Script"))
					strcpy(font->family, "script");
				else if(strstr(font->baseFont, "Chancery"))
					strcpy(font->family, "decor");
				else if(strstr(font->baseFont, "Symbol") || strstr(font->baseFont, "Dingbats"))
					strcpy(font->family, "tech");
				else
					strcpy(font->family, "nil");
			}
			
			font->index = mFontTable.size();
			
			mFontTable.push_back(font);
			return;
		}
		
		if(font->key)
			free(font->key);
		
		if(font->baseFont)
			free(font->baseFont);
						
		if(font->family)
			free(font->family);
						
		if(font->map)
			free(font->map);
			
		if(font->widths)
			free(font->widths);
			
		free(font);
	}
}

CPDFParser::PDFFontObject*
CPDFParser::GetFont(
	const char* inKey)
{
	std::vector<PDFFontObject*>::const_iterator i = mFontTable.begin();
	PDFFontObject* f;
	
	for(i = mFontTable.begin(); i != mFontTable.end(); i++) {
		f = *i;
		
		if(strcmp(inKey, f->key) == 0)
			return f;
	}
	
	return NULL;
}

CPDFParser::PDFFontObject*
CPDFParser::GetFont(
	long inIndex)
{
	return mFontTable[inIndex];
}

void
CPDFParser::FreeFonts()
{
	std::vector<PDFFontObject*>::const_iterator i = mFontTable.begin();
	PDFFontObject* f;
	
	for(i = mFontTable.begin(); i != mFontTable.end(); i++) {
		f = *i;
		
		free(f->key);
		free(f->baseFont);
		
		if(f->family)
			free(f->family);
		
		if(f->map)
			free(f->map);
		
		if(f->umap)
			free(f->umap);
		
		if(f->widths)
			free(f->widths);
		
		free(f);
	}
		
	mFontTable.clear();
}

void
CPDFParser::AddTab(
	long inCol)
{
	if(GetTabIndex(inCol) != -1)
		return;	
		
	mTabTable.push_back(inCol);
}

char*
CPDFParser::GetTabString()
{
	char* outTabs = NULL;
	
	long tabStringLength = 0;
	char tabBuf[16];
	
	{
		std::vector<long>::const_iterator i = mTabTable.begin();
		long tab;
		
		for(i = mTabTable.begin(); i != mTabTable.end(); i++) {
			tab = *i;
			
			long tabValue = lroundf((float) tab / xs);
			if(mType == kWriteRTFWord)
				tabValue -= mLeftMargin;
				
			sprintf(tabBuf, "\\tx%d", 20 * (int)tabValue);
			
			tabStringLength += strlen(tabBuf);
		}
	}
	
	if(tabStringLength) {
		tabStringLength++;
		
		outTabs = (char*) malloc(tabStringLength);
	}
	
	tabStringLength = 0;
	
	if(outTabs) {
		std::vector<long>::const_iterator i = mTabTable.begin();
		long tab;
		
		for(i = mTabTable.begin(); i != mTabTable.end(); i++) {
			tab = *i;
			
			long tabValue = lroundf((float) tab / xs);
			if(mType == kWriteRTFWord)
				tabValue -= mLeftMargin;
				
			sprintf(tabBuf, "\\tx%d", 20 * (int)tabValue);
			
			long len = strlen(tabBuf);
			memcpy(&(outTabs[tabStringLength]), tabBuf, len);
			tabStringLength += len;
		}
		
		outTabs[tabStringLength] = 0;
	}
		
	return outTabs;
}

long
CPDFParser::GetTabsToCol(
	long inStart,
	long inEnd)
{
	std::vector<long>::const_iterator i = mTabTable.begin();
	long tab;
	
	long tabsToCol = 0;
	
	for(i = mTabTable.begin(); i != mTabTable.end(); i++) {
		tab = *i;
		
		if(tab > inStart && tab <= inEnd)
			tabsToCol++;
	}
	
	return tabsToCol;
}

long
CPDFParser::GetTabIndex(
	long inCol)
{
	std::vector<long>::const_iterator i = mTabTable.begin();
	long tab;
	
	long tabIndex = 0;
	
	for(i = mTabTable.begin(); i != mTabTable.end(); i++) {
		tab = *i;
		
		if(tab == inCol)
			return tabIndex;
			
		tabIndex++;
	}
	
	return -1;
}

void
CPDFParser::FreeTabs()
{
	mTabTable.clear();
}

CPDFParser::PDFEncoder*
CPDFParser::AddEncoder(
	TextEncoding inEncoding)
{
	PDFEncoder* encoder = GetEncoder(inEncoding);
	
	if(encoder == NULL) {
		encoder = (PDFEncoder*) malloc(sizeof(PDFEncoder));
		
		if(encoder) {
			encoder->encoding = inEncoding;
			encoder->tec = NULL;
			
			if(inEncoding != mEncodingOut) {
#if defined(WIN32)
				// todo: add .NET text encoding support
#else
				TECObjectRef tec;
				if(::TECCreateConverter(&tec, inEncoding, mEncodingOut) == noErr)
					encoder->tec = tec;
#endif					
			}
						
			mEncoders.push_back(encoder);
		}
	}
		
	return encoder;
}

CPDFParser::PDFEncoder*
CPDFParser::GetEncoder(
	TextEncoding inEncoding)
{
	std::vector<PDFEncoder*>::const_iterator i = mEncoders.begin();
	PDFEncoder* e;
	
	for(i = mEncoders.begin(); i != mEncoders.end(); i++) {
		e = *i;
		
		if(inEncoding == e->encoding)
			return e;
	}
	
	return NULL;
}

void
CPDFParser::FreeEncoders()
{
	std::vector<PDFEncoder*>::const_iterator i = mEncoders.begin();
	PDFEncoder* e;
	
	for(i = mEncoders.begin(); i != mEncoders.end(); i++) {
		e = *i;
		
		if(e->tec) {
#if defined(WIN32)
		// todo: add .NET text encoding support
#else
			::TECDisposeConverter(e->tec);
#endif
		}
		
		free(e);
	}
		
	mEncoders.clear();
}

#pragma mark -

void
CPDFParser::Normalize(
	float width,
	float height)
{
	float xmin = 0.0;
	float ymin = 0.0;
		
	{
		std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
		PDFTextObject* t;
		
		for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
			t = *i;

			if(t->x < xmin)
				xmin = t->x;
				
			if(t->y < ymin)
				ymin = t->y;
			
			if(t->x > xmax)
				xmax = t->x;
				
			if(t->y > ymax)
				ymax = t->y;
		}
	}

	bool flipX = false;
	if(fequal_(xmax, 0.0)) {
		flipX = true;
		xmax = -xmin;
	}
	
	bool flipY = false;
	if(fequal_(ymax, 0.0)) {
		flipY = true;
		ymax = -ymin;
	}		
	
	// adjust for misalignment (rare)
	if(xmax < width)
		xmax = width;

	if(ymax < height)
		ymax = height;
		
	float cx = 0.0;
	float cy = 0.0;	
		
	// flip coordinate space (really rare)
	if(flipX || flipY) {
		std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
		PDFTextObject* t;
		
		for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
			t = *i;

			if(flipX) {
				t->x += xmax;
				t->tx += xmax;
			}
			
			if(flipY) {
				t->y += ymax;
				t->ty += ymax;
			}
			
			if(t->x > cx)
				cx = t->x;
				
			if(t->y > cy)
				cy = t->y;
		}
	}
	
	// adjust for misscale (really rare)
	bool scaleX = false;
	bool scaleY = false;
	
	if(cx > width)
		scaleX = true;

	if(cy > height)
		scaleY = true;

	if(scaleX || scaleY) {
		float xa = (scaleX && cx ? (width / cx) : 1.0);
		float ya = (scaleY && cy ? (height / cy) : 1.0); 
		
		std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
		PDFTextObject* t;
		
		for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
			t = *i;

			if(scaleX) {
				t->x *= xa;
				t->tx *= xa;
			}
			
			if(scaleY) {
				t->y *= ya;
				t->ty *= ya;
			}
		}
	}
}

void
CPDFParser::Fit()
{
	std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
	PDFTextObject* t;
	
	float y;

	i = mPageObjects.begin();
	if(i != mPageObjects.end()) {
		t = *i;

		t->col = lroundf(t->x * xs);
		t->line = lroundf((ymax - t->y) * ys);
		
		y = t->y;
		i++;
	}
	
	while(i != mPageObjects.end()) {
		t = *i;
					
		t->col = lroundf(t->x * xs);
		t->line = lroundf((y - t->y) * ys);
		
		y = t->y;
		i++;
	}
}

void
CPDFParser::CalcLines()
{
	float currentX = 0.0;
	
 	{
		std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
		PDFTextObject* t;
		
		std::vector<PDFTextObject*>::const_iterator head = mPageObjects.begin();
		bool allWhitespace = false;
		
		for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
			t = *i;

			if(t->text) {
				bool insertNewline = false;
				
				if(t->line)
					insertNewline = true;
				else if(t->x < currentX)
					insertNewline = true;
					
				if(insertNewline) {
					if(allWhitespace) {
 						// filter out isolated whitespace objects
						while(head < i) {
							PDFTextObject* s = *head;
							s->size = 0;
							
							head++;
						}
					}
					
					head = i;
					
					if(t->ws)
						allWhitespace = true;
					else
						allWhitespace = false;
				}
				else {
					if(allWhitespace && t->ws == false)
						allWhitespace = false;
					else if(allWhitespace == false && t->ws) {
						head = i;
						allWhitespace = true;
					}
				}
	
				currentX = t->x;
			}
		}
	}
}

long
CPDFParser::CalcWhitespace()
{
	float leading = 1.0;
	if(mType == kWriteRTF)
		leading = 1.0 / kRTFSpacing;
		
	float currentX = 0.0;
	float currentY = ymax;
	float currentF = 0.0;
	long currentCol = 0;
	
	float basex = 1.0 / xs;
	float basey = 1.0 / ys;
	float basef = (2.0 * basex) + EPS;
	
	long ws = 0;
	
	{
		std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
		PDFTextObject* t;
		
		for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
			t = *i;

			if(t->text && t->size) {
				bool insertNewline = false;
				
				if(t->line)
					insertNewline = true;
				else if(t->x < currentX)
					insertNewline = true;

				if(insertNewline) {
					long w = 1;	
					
					if(mLine && mPadLines) {
						float fontsize = currentF;
						if(mType < kWriteRTF || fequal_(fontsize, 0.0))
							fontsize = basey;
							
						float delta = leading * (currentY - t->y);
						
						w = lroundf(delta / fontsize);
						
						if(w > 2) {
							delta -= (t->f * kRTFSpacing);
							w = lroundf(delta / fontsize);	
						}
							
						if(w < 1)
							w = 1;
						else if(w > mLine)
							w = mLine;
					}
					
					currentCol = 0;
															
					ws += w;
					t->line = w;
	
					if(mCol && mPadCols) {
						long w = t->col;
						if(w < 0)
							w = 0;
						else if(w > mCol)
							w = mCol;	
																		
						ws += w;
						t->col = w;

						currentCol += t->col;							
					}
					else
						t->col = 0;
				}
				else {
					if(mPadCols && mCol && t->ws == false) {
						long w = t->col;
						if(w < 0)
							w = 0;
						else if(w > mCol)
							w = mCol;
						
						if(w > currentCol) {	
							long offset = w - currentCol;
							
							long midOffset = offset;
							if(mType >= kWriteRTF && t->f > basef)
								midOffset = lroundf(((float) offset * basex) / t->f);
																				
							if(mRelaxSpacing || midOffset >= kMinMidlineSpacing) {
								if(mType >= kWriteRTF && mPadStrip == false) {
									AddTab(w);
									currentCol = t->col;
								}
								else
									currentCol += offset;
							}
							else
								w = offset + currentCol;
						}
														
						ws += w;
						t->col = w;		
					}
					else
						t->col = 0;
				}
												
				currentX = t->x;
				currentY = t->y;
				
				if(fequal_(t->y, t->ty))
					currentF = t->f;
				
				currentCol += t->width;
			}
		}
	}
	
	if(mType >= kWriteRTF && mPadStrip == false)
		stable_sort(mTabTable.begin(), mTabTable.end(), CTabSorter()); // sort tab table
	else {
		mPageSpacing = 2;
		
		if(mLine) {
			if(mType < kWriteRTF || fequal_(currentF, 0.0))
				mPageSpacing = lroundf(leading * currentY * ys);
			else
				mPageSpacing = lroundf(leading * currentY / currentF);

			if(mPageSpacing < 2)
				mPageSpacing = 2;
			else if(mPageSpacing > mLine)
				mPageSpacing = mLine;
		}
		
		ws += mPageSpacing;
	}
		
	return ws;	
}

void
CPDFParser::CalcExtraTabs()
{
	if(mCol == 0)
		return;
		
	bool didAddTabs = false;	
		
	long* spread = (long*) calloc(mCol + 1, sizeof(long));
	
	if(spread) {
		std::vector<PDFTextObject*>::const_iterator i = mPageObjects.begin();
		PDFTextObject* t;
	
		long lineCount = 0;
		
		for(i = mPageObjects.begin(); i != mPageObjects.end(); i++) {
			t = *i;

			if(t->text && t->size && t->ws == false) {
				if(t->line)
					lineCount++;
				
				if(t->col) {
					if(GetTabIndex(t->col) == -1)
						spread[t->col]++;
				}
			}
		}
		
		lineCount /= 2;
		if(lineCount < 1)
			lineCount = 1;

		// add tabs for popular (at least half of lines on page), untabbed x positions
		
		long threshold = 144.0 * xs; // limit tabs to two inches from left side of page
		
		for(long j = threshold; j <= mCol; j++) {
			if(spread[j] > lineCount) {
				AddTab(j);
				didAddTabs = true;
			}
		}		
			
		free(spread);
	}
	
	if(didAddTabs)
		stable_sort(mTabTable.begin(), mTabTable.end(), CTabSorter()); // sort tab table
}

#pragma mark -

void
CPDFParser::InitMetrics()
{
	// PDF text space
	mL = 0.0;
	mX = 0.0;
	mY = 0.0;
	mLX = 0.0;
	mS = 0.0;
	mTC = 0.0;
	mTW = 0.0;
	
	mArrayJ = 0.0;
	mArrayX = 0.0;
	
	mLastL = 0.0;
	mLastF = 1.0;
	mLastFS = 1.0;
	mLastX = 0.0;
	mLastY = 0.0;
	mTrueY = 0.0;
	
	for(long i = 0; i < kMaxOperands; i++)
		mOperand[i] = 0.0;
}

void
CPDFParser::InitChunker()
{
	c = NULL;
	cd = 0;
}

void
CPDFParser::OpenChunker(
	long inSize)
{
	if(c == NULL) {
		if(inSize <= 4096)
			inSize = 4096;
		else {
			long mod = (inSize % 4096);
			if(mod)
				inSize += (4096 - mod);
		}
		
		c = (unsigned char *) malloc(inSize);
		cd = 0;
	}
}

void
CPDFParser::CloseChunker()
{
	if(c) {
		free(c);
		c = NULL;

		cd = 0;
	}
}

void
CPDFParser::ProcessChunk()
{
	mTrueY = mY;
	
	if(mAllWhitespace == false && cd <= kMinScriptLength && mX > mLastX + EPS && fequal_(mLastY, 0.0) == false) {
		bool mayBeSuperOrSub = true;
		
		for(long i = 0; i < cd && mayBeSuperOrSub; i++) {
			if(
				isspace(c[i]) == false &&
				isdigit(c[i]) == false &&
				c[i] != '.' &&
				c[i] != '-' &&
				c[i] != '(' &&
				c[i] != ')' &&
				c[i] != '[' &&
				c[i] != ']'
			)
				mayBeSuperOrSub = false;
		}
		
		if(cd == 2) {
			if(c[0] == 's' && c[1] == 't')
				mayBeSuperOrSub = true;
			else if(c[0] == 'n' && c[1] == 'd')
				mayBeSuperOrSub = true;
			else if(c[0] == 'r' && c[1] == 'd')
				mayBeSuperOrSub = true;
			else if(c[0] == 't' && c[1] == 'h')
				mayBeSuperOrSub = true;
			else if(c[0] == 'T' && c[1] == 'M')
				mayBeSuperOrSub = true;
		}
		else if(cd == 1)
			mayBeSuperOrSub = true;
						
		if(mayBeSuperOrSub) {
			if(mY > mLastY + EPS) {			
				float fontsize = (mLastF * mLastFS);
				float yd;
				
				if(fequal_(fontsize, 0.0))
					yd = mLastY + lroundf(1.0 / ys);
				else
					yd = mLastY + fontsize;

				// push superscripts back into line
				if(mY < yd)
					mY = mLastY;
			}
			else if(mY < mLastY - EPS) {
				float fontsize = (mF * mFS);
				float yd = mY + fontsize;
					
				// push subscripts back into line
				if(yd > mLastY)
					mY = mLastY;
			}
		}
	}
	
	mLastF = mF;
	mLastFS = mFS;
	mLastL = mL;
	mLastX = mX;
	mLastY = mY;

	AddTextToPage();
	
	CloseChunker();
}

void
CPDFParser::PadChunk(
	long pad)
{
	unsigned char* buffer = (unsigned char*) malloc(cd + (cd * pad));
	if(buffer == NULL)
		return;
		
	long bufferSize = 0;
	long i = 0;
	
	for(i = 0; i < cd - 1; i++) {
		buffer[bufferSize++] = c[i];
		
		for(long j = 0; j < pad; j++)
			buffer[bufferSize++] = ' ';
	}

	buffer[bufferSize++] = c[i];
	
	if(bufferSize) {					
		free(c);
		c = buffer;
		cd = bufferSize;
	}
	else
		free(buffer);
}

#pragma mark -

CPDFParser::PDFTextObject*
CPDFParser::AddTextToPage(
	unsigned char* inText,
	long inSize)
{
	bool killBuffer = false;
	
	long width = 0; // actual number of characters to fit when rendering
	float textWidth = 0.0;
			
	if(inText == NULL) {
		inText = c;
		inSize = cd;
		width = inSize;

		mAllWhitespace = true;
		
		for(long i = 0; i < inSize && mAllWhitespace; i++) {
			if(isspace(inText[i]) == false)
				mAllWhitespace = false;
		}

		if(mAllWhitespace == false && inSize > 1 && mFont && mFont->widths) {
			// if character spacing is large enough, space out this text
			// (sometimes used to center page numbers)
			if(mTC > 1.0) {
				float scale = (fequal_(mFS, 0.0) ? 1.0 : mFS);
				long wedge = lroundf(mTC / scale);
				
				if(mRelaxSpacing || wedge > kMinMidlineSpacing) {
					float saveTC = mTC;
					float saveX = mX;
					
					mTC = 0.0;
				
					for(long i = 0; i < cd; i++) {
						if(isspace(c[i]) == false)	
							AddTextToPage(&(c[i]), 1);
							
						float offset = GetCharacterWidth(c[i]);
						float span = (offset / 1000.0) + saveTC;
						mX += mF * span;
					}

					mTC = saveTC;
					mX = saveX;	
					
					mAllWhitespace = false;
					
					return NULL;
				}
									
				// just pad each character with spaces
				if(wedge) {
					PadChunk(wedge);
				
					inText = c;
					inSize = cd;
					
					width = inSize;
				}
			}

			// if initial word spacing is large enough, space out this text
			// (this is a common indent strategy)
			if(mTW > 1.0) {
				long i = 0;
				long j = 0;
				
				float saveTW = mTW;
				float saveX = mX;
				
				mTW = 0.0;

				for(i = 0; i < cd; i++) {
					if(c[i] == ' ') {
						float textSpace = 0.0;
						
						if(i > j) {
							textSpace = GetTextWidth(&(c[j]), (i - j));
							
							AddTextToPage(&(c[j]), 1 + (i - j));
							j = i + 1;
						}
						
						float span = (textSpace / 1000.0) + saveTW;
						mX += mF * span;
						
						i = cd - 1; // only process the first space
					}
				}
				
				if(i > j)
					AddTextToPage(&(c[j]), (i - j));
								
				mTW = saveTW;
				mX = saveX;
				
				mAllWhitespace = false;
				
				return NULL;
			}
		}
		
		if(mFont) {
			if(mType >= kWriteRTF && mFont->widths)
				textWidth = GetTextWidth(inText, inSize);
				
			unsigned char* map = Map(inText, inSize);
			if(map != inText) {
				free(c);
				c = map;
				cd = inSize;

				inText = c;
			}
			
			unsigned char* encode = Encode(inText, inSize);		
			if(encode != inText) {
				free(c);
				c = encode;
				cd = inSize;

				inText = c;
			}
		}
	}
	else {
		width = inSize;
		
		if(mFont) {
			if(mType >= kWriteRTF && mFont->widths)
				textWidth = GetTextWidth(inText, inSize);

			unsigned char* map = Map(inText, inSize);
			if(map != inText) {
				inText = map;
				killBuffer = true;
			}
				
			unsigned char* encode = Encode(inText, inSize);		
			if(encode != inText) {
				if(killBuffer)
					free(inText);
					
				inText = encode;
				killBuffer = true;
			}
		}
	}

#ifdef WEBSONAR
	/* websonar.com mod */
	bool isNumeric = true;
	for(long i = 0; i < inSize && isNumeric; i++) {
		if(isdigit(inText[i]) == false)
			isNumeric = false;
	}

	if(isNumeric)
		mX -= 24.0;
#endif
	
	float pagex = mX;
	float pagey = mY;
	float truey = mTrueY;
	
	if(mCrop) {
		pagex -= mCropWidth;
		pagey -= mCropHeight;
		truey -= mCropHeight;
		
		if(pagex < 0.0 || pagex > (float) mPageWidth || pagey < 0.0 || pagey > (float) mPageHeight) {
			if(killBuffer)
				free(inText);

			return NULL;
		}
	}
	
	PDFTextObject* t = (PDFTextObject*) malloc(sizeof(PDFTextObject));

	if(t) {
		t->pre = NULL;
		t->preSize = 0;
		t->post = NULL;
		t->postSize = 0;

#ifdef SHOWCOORDS		
		char coord[256];
		sprintf(coord, "[%.2f, %.2f]", mX, mY);
		long lc = strlen(coord);
		
		t->text = (unsigned char*) malloc(lc + inSize);
		
		if(t->text) {
			t->f = (mF * mFS);
			t->x = pagex;
			t->y = pagey;
			t->size = lc + inSize;
			t->width = width;
			t->font = mFont;
			t->ws = mAllWhitespace;
			
			t->tx = t->x;
			if(mType >= kWriteRTF) {
				if(mFont && mFont->widths)
					t->tx += (t->f * (textWidth / 1000.0));
				else
					t->tx += (t->f * ((float) inSize * .5));
			}
			
			t->ty = truey;

			memcpy(t->text, coord, lc);
			memcpy(&(t->text[lc]), inText, inSize);
			if(killBuffer)
				free(inText);
			
			mPageLength += t->size;
			mPageWeight += lroundf(t->f * (float) t->width);
			mPageWidths += t->width;
			
			mPageObjects.push_back(t);
						
			return t;
		}
#else
		t->text = (unsigned char*) malloc(inSize);
		
		if(t->text) {
			t->f = (mF * mFS);
			t->x = pagex;
			t->y = pagey;
			t->size = inSize;
			t->width = width;
			t->font = mFont;
			t->ws = mAllWhitespace;

			t->tx = t->x;
			if(mType >= kWriteRTF) {
				if(mFont && mFont->widths)
					t->tx += (t->f * (textWidth / 1000.0));
				else
					t->tx += (t->f * ((float) inSize * .5));
			}
				
			t->ty = truey;

			//for(long i = 0; i < inSize; i++) {
			//	if(inText[i] == 0)
			//		DebugStr("\pnull");
			//}

			memcpy(t->text, inText, inSize);
			if(killBuffer)
				free(inText);

			mPageLength += t->size;
			mPageWeight += lroundf(t->f * (float) t->width);
			mPageWidths += t->width;
			
			mPageObjects.push_back(t);
			
			return t;
		}
#endif
		
		if(killBuffer)
			free(inText);

		free(t);
	}
	
	return NULL;
}

long
CPDFParser::MapUnicode(
	wchar_t inCode)
{
	// look for the code in case its already stuffed
	if(mFont->umap) {
		for(long i = 1; i <= 255; i++) {
			if(mFont->umap[i] == inCode)
				return i;
		}
	}

	// check to see if we need a unicode mapping array for this font
	if(mFont->umap == NULL) {
		mFont->umap = (wchar_t*) calloc(256, sizeof(wchar_t));
		
		if(mFont->umap)
			mFont->mapInPlace = false;
	}
	
	if(mFont->umap) {
		for(long i = 1; i <= 255; i++) {
			if(mFont->umap[i] == 0) {
				mFont->umap[i] = inCode;
				return i;
			}
		}
	}
	
	return 0;
}		

unsigned char*
CPDFParser::Map(
	unsigned char* inText,
	long& inSize)
{
	if(mFont->mapInPlace)
		return inText;
		
	if(mFont->umap == NULL && mFont->map == NULL && mFont->fi == 0 && mFont->fl == 0)
		return inText;
		
	/*if(mFont->umap == NULL && mFont->fi == 0 && mFont->fl == 0) {
		for(long i = 0; i < inSize; i++)
			inText[i] = mFont->map[inText[i]];

		return inText;
	}*/	
		
	unsigned char* buffer = (unsigned char*) malloc(inSize * 10);
	if(buffer == NULL)
		return inText;
		
	long bufferSize = 0;

	for(long i = 0; i < inSize; i++) {
		wchar_t u = (mFont->umap ? mFont->umap[inText[i]] : 0);
		
		if(u) {
			if(mType >= kWriteRTF) {
				char us[16];
				sprintf(us, "{\\u%d?}", u);
				
				long len = strlen(us);
				memcpy(&(buffer[bufferSize]), us, len);
				bufferSize += len;
			}
			else if(mType == kWritePlainText)
				inText[i] = '\245';
			else
				inText[i] = '-';
		}
		else if(inText[i] == mFont->fi) { // ligature
			buffer[bufferSize++] = 'f';
			buffer[bufferSize++] = 'i';
		}
		else if(inText[i] == mFont->fl) { // ligature
			buffer[bufferSize++] = 'f';
			buffer[bufferSize++] = 'l';
		}
		else {
			if(mFont->map)
				inText[i] = mFont->map[inText[i]];
				
			//if(bufferSize)
				buffer[bufferSize++] = inText[i];
		}
	}
	
	if(bufferSize) {
		inSize = bufferSize;
		return buffer;
	}
	
	free(buffer);
		
	return inText;
}

unsigned char*
CPDFParser::Encode(
	unsigned char* inText,
	long& inSize)
{
	PDFEncoder* encoder = mFont->encoder;
			
	if(encoder == NULL)
		return inText;
								
#if defined(WIN32)
		// todo: add .NET text encoding support
#else
	if(mEncodingOut == kEncodeASCII) {
		unsigned char bullet = (encoder->encoding == kEncodeMacSymbol ? '\267' : '\245');
		
		for(long i = 0; i < inSize; i++) {
			if(inText[i] == bullet)
				inText[i] = '-'; // nonstandard mac bullet conversion
		}
	}
	
	TECObjectRef tec = encoder->tec;
	if(tec) {
		bool didEncode = false;
		
		long bufferSize = inSize * 8;
		if(bufferSize < 32)
			bufferSize = 32;
			
		unsigned char* buffer = (unsigned char*) malloc(bufferSize);
		
		if(buffer) {	
			ByteCount encodedIn = 0;
			ByteCount encodedOut = 0;
			ByteCount flushedOut = 0;
				
			if(::TECConvertText(tec, inText, inSize, &encodedIn, buffer, bufferSize, &encodedOut) == noErr) {
				if(::TECFlushText(tec, &(buffer[encodedOut]), bufferSize - encodedOut, &flushedOut) == noErr)
					didEncode = true;
			}
		
			::TECClearConverterContextInfo(tec);			
			
			if(didEncode) {
				inSize = encodedOut + flushedOut;
				return buffer;
			}
			
			free(buffer);
		}
	}
#endif

	return inText;	
}

float
CPDFParser::GetCharacterWidth(
	unsigned char c)
{
	if(mFont && mFont->widths)
		return mFont->widths[c];
		
	return 0.0;
}

float
CPDFParser::GetTextWidth(
	unsigned char* inText,
	long inSize)
{
	float outWidth = 0.0;
	
	if(mFont && mFont->widths) {
		if(inText == NULL) {
			inText = c;
			inSize = cd;
		}
		
		for(long i = 0; i < inSize; i++)
			outWidth += mFont->widths[inText[i]];
	}
		
	return outWidth;
}
