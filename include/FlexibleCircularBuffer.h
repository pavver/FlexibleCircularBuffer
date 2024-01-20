#pragma once

#include <cstring>

template <typename BuffT>
struct BufferLine
{
public:
  /// Get the data
  const BuffT *GetData() const
  {
    return _data;
  }

  /// Get the length
  uint16_t GetLength() const
  {
    return _length;
  }

  /// Get the id
  uint32_t GetId() const
  {
    return _id;
  }

  /// Destructor
  ~BufferLine()
  {
    if (_needFree)
      free(_data);
  }

protected:
  /// Constructor
  BufferLine(BuffT *data, uint16_t length, uint32_t id, bool needFree)
      : _data(data),
        _id(id),
        _length(length),
        _needFree(needFree)
  {
  }

  /// The data
  BuffT *_data;
  /// The id
  const uint32_t _id;
  /// The length data
  const uint16_t _length;
  /// Whether the data needs to be freed
  const bool _needFree;
};

/// @brief Buffer line with editable content.
/// @tparam BuffT Type of the buffer.
template <typename BuffT>
class EditableBufferLine : public BufferLine<BuffT>
{
public:
  /// @brief Constructor.
  /// @param data Pointer to the data in buffer or pointer to the allocated data, if line in buffer is fragmented.
  /// @param length Length of the buffer.
  /// @param id Identifier of the line.
  /// @param needFree Flag indicating if the buffer needs to be freed (if line in buffer is fragmented).
  EditableBufferLine(BuffT *data, uint16_t length, uint32_t id, bool needFree = false)
      : BufferLine<BuffT>(data, length, id, needFree)
  {
  }
};

struct BufferLineMarker
{
public:
  /// Start index of the line in buffer data.
  int16_t startIndex = 0;
  /// End index of the line in buffer data.
  int16_t endIndex = 0;
  /// Identifier of the line.
  uint32_t id = 0;

  /// @brief Check if the line intersects with the given line.
  bool inIntersection(BufferLineMarker &line)
  {
    // Check if both lines are not fragmented.
    if (startIndex < endIndex && line.startIndex < line.endIndex)
    {
      // If the start index is less than the line start index, the intersection
      // is at the start of the line.
      if (startIndex <= line.startIndex)
        return endIndex >= line.startIndex;
      // Otherwise, the intersection is at the end of the line.
      return line.endIndex >= startIndex;
    }
    // if both lines are fragmented, then they exactly intersect.
    if (startIndex > endIndex && line.startIndex > line.endIndex)
      return true;
    // we check depending on which of the lines is fragmented
    if (startIndex > endIndex)
      return line.startIndex <= endIndex || line.endIndex >= startIndex;
    return startIndex <= line.endIndex || endIndex >= line.startIndex;
  }
};

/// @brief circular buffer for data of different lengths
/// @tparam BuffT Type of the buffer.
template <typename BuffT>
class FlexibleCircularBuffer
{
public:
  FlexibleCircularBuffer(uint16_t bufferSize = 4096, uint16_t maxLines = 128)
      : _bufferSize(bufferSize),
        _maxLines(maxLines)
  {
    buff = new BuffT[_bufferSize];
    lines = new BufferLineMarker[_maxLines];
  }

  ~FlexibleCircularBuffer()
  {
    delete[] buff;
    delete[] lines;
  }

  /// @brief Write new line to buffer
  /// @param data data
  /// @param length data length
  /// @return id of the created line. 0 if error
  uint32_t WriteLine(const BuffT *data, uint16_t length)
  {
    // If the length of the data is 0, return 0.
    if (length == 0)
      return 0;

    // for the correct operation of the algorithm, there must always be at least two active lines in the buffer.
    // therefore, we check that the new lines data does not occupy more than half of the buffer.
    if (length > _bufferSize / 2)
      return 0;

    // Get the next index to write to.
    int16_t nextIndex = getNextIndex(_indexLastLine);

    // Create a new line.
    BufferLineMarker newLine;
    newLine.endIndex = 0;

    // if the data fits into the buffer without fragmentation, then we simply write it to the buffer
    if ((lines[_indexLastLine].endIndex + length + 1) < _bufferSize)
    {
      memcpy(_indexLastLine == -1 ? buff : (buff + lines[_indexLastLine].endIndex + 1), data, length * sizeof(BuffT));
      newLine.endIndex = (_indexLastLine == -1 ? 0 : lines[_indexLastLine].endIndex + 1) + length - 1;
    }
    else
    {
      // Otherwise, we need to split the line.
      memcpy(_indexLastLine == -1 ? buff : (buff + lines[_indexLastLine].endIndex + 1),
             data,
             (_bufferSize - lines[_indexLastLine].endIndex - 1) * sizeof(BuffT));
      newLine.endIndex = _bufferSize - lines[_indexLastLine].endIndex - 1;
      data += newLine.endIndex;
      length -= newLine.endIndex;

      // Write the rest of the data to a new line.
      memcpy(buff, data, length * sizeof(BuffT));
      newLine.endIndex = length - 1;
    }

    // If this is the first line, then it is the first line.
    if (_indexLastLine == -1)
    {
      newLine.startIndex = 0;
      newLine.id = 0;
    }
    else
    {
      // Otherwise, we need to find the start index of the new line.
      newLine.startIndex = (lines[_indexLastLine].endIndex + 1) % _bufferSize;
      newLine.id = lines[_indexLastLine].id + 1;
    }

    // if the data of the line we just recorded intersects with the previously recorded lines,
    // then we erase the data about the overwritten lines.
    if (_indexFirstLine == -1)
      _indexFirstLine = nextIndex;
    else
    {
      BufferLineMarker line = lines[_indexFirstLine];
      bool InIntersection = line.inIntersection(newLine);
      while (InIntersection)
      {
        _indexFirstLine = getNextIndex(_indexFirstLine);
        line = lines[_indexFirstLine];
        InIntersection = line.inIntersection(newLine);
      }
    }

    // Set the new line as the last line.
    _indexLastLine = nextIndex;
    lines[_indexLastLine] = newLine;

    // Return the id of the new line.
    return newLine.id;
  }

  /// @brief Read the first buffer line
  /// @return BufferLine or nullptr if empty
  BufferLine<BuffT> *ReadFirst()
  {
    if (_indexFirstLine < 0)
      return nullptr;
    return CreateBufferLine(_indexFirstLine);
  }

  /// @brief Read the last buffer line
  /// @return BufferLine or nullptr if empty
  BufferLine<BuffT> *ReadLast()
  {
    if (_indexLastLine < 0)
      return nullptr;
    return CreateBufferLine(_indexLastLine);
  }

  /// @brief Read the next buffer line with given id
  /// @return BufferLine or nullptr if not found
  BufferLine<BuffT> *ReadNext(uint32_t id)
  {
    if (_indexLastLine < 0)
      return nullptr;

    // Find the next line with given id
    for (int16_t index = _indexFirstLine; index != _indexLastLine; index = getNextIndex(index))
    {
      // If the current line has the given id, return it
      if (lines[index].id == id)
        return CreateBufferLine(getNextIndex(index));
    }

    // If there are no more lines, return nullptr
    return nullptr;
  }

  // Delete the current one, and return next line.
  BufferLine<BuffT> *FreeAndReadNext(BufferLine<BuffT> *line)
  {
    // Get the ID of the line
    uint32_t id = line->GetId();
    // Delete the current line
    delete line;
    // Return the next line with the same ID
    return ReadNext(id);
  }

private:
  // Pointer to the buffer
  BuffT *buff;
  // Pointer to the buffer line marker
  BufferLineMarker *lines;

  // Buffer size
  const uint16_t _bufferSize;
  // Maximum number of lines
  const uint16_t _maxLines;

  // Index of the first line
  int16_t _indexFirstLine = -1;

  // Index of the last line
  int16_t _indexLastLine = -1;

  /// @brief Get the next index in the text buffer.
  /// @param[in] index The current index.
  int16_t getNextIndex(int16_t index)
  {
    // Return the next index in the buffer.
    return (index + 1) % _maxLines;
  }

  /// @brief Get the previous index in the text buffer.
  /// @param[in] index The current index.
  int16_t getPrevIndex(int16_t index)
  {
    // Return the previous index in the buffer.
    return (index + _maxLines - 1) % _maxLines;
  };

  /// @brief Creates a BufferLine with a pointer to the data in the buffer, or if the data is fragmented, allocates memory and copies the fragments there
  /// @param index
  /// @return
  BufferLine<BuffT> *CreateBufferLine(int16_t index)
  {
    // If the line is not fragmented, return a new editable buffer line with pointer.
    if (lines[index].startIndex < lines[index].endIndex)
      return new EditableBufferLine<BuffT>(&buff[lines[index].startIndex], (lines[index].endIndex - lines[index].startIndex + 1), lines[index].id);

    // Otherwise, create a new line from fragments.
    uint16_t length = _bufferSize - lines[index].startIndex + lines[index].endIndex + 1;
    BuffT *lineData = (BuffT *)malloc(sizeof(BuffT) * length);

    // Copy the existing data into the new line.
    memcpy(lineData, buff + lines[index].startIndex, (_bufferSize - lines[index].startIndex) * sizeof(BuffT));
    memcpy(lineData + _bufferSize - lines[index].startIndex, buff, (lines[index].endIndex + 1) * sizeof(BuffT));

    // Return a new editable buffer line from the new line data.
    return new EditableBufferLine<BuffT>(lineData, length, lines[index].id, true);
  }
};
