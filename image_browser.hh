#pragma once



#include <ctime>
#include <filesystem>
#include <fstream>
#include <queue>
#include <string>
#include "cd.hh"
#include "iso9660.hh"



namespace gpsxre
{

class ImageBrowser
{
public:
	class Entry
	{
		friend class ImageBrowser;

	public:
		bool IsDirectory() const;
		std::list<std::shared_ptr<Entry>> Entries();
		std::shared_ptr<Entry> SubEntry(const std::filesystem::path &path);
		const std::string &Name() const;
		uint32_t Version() const;
		time_t DateTime() const;
		bool IsDummy() const;
		bool IsInterleaved() const;

		uint32_t SectorOffset() const;
		uint32_t SectorSize() const;

		std::vector<uint8_t> Read(bool form2 = false, bool throw_on_error = false);
		//DEBUG
		std::vector<uint8_t> Peek();
//		std::vector<uint8_t> Read(uint32_t data_offset, uint32_t size);
//		std::set<uint8_t> ReadMode2Test();
//		std::vector<uint8_t> Read(std::set<uint8_t> *xa_channels = NULL);
//		std::vector<uint8_t> ReadXA(uint8_t channel);

	private:
		ImageBrowser &_browser;
		std::string _name;
		uint32_t _version;
		iso9660::DirectoryRecord _directory_record;

		Entry(ImageBrowser &browser, const std::string &name, uint32_t version, const iso9660::DirectoryRecord &directory_record);

		bool DirectoryRecordValid(const iso9660::DirectoryRecord &dr) const;
	};

	static bool IsDataTrack(const std::filesystem::path &track);

	ImageBrowser(const std::filesystem::path &data_track, uint32_t file_offset, bool scrambled);
	ImageBrowser(std::fstream &fs, uint32_t file_offset, bool scrambled);

	std::shared_ptr<Entry> RootDirectory();

    const iso9660::VolumeDescriptor &GetPVD() const;

	template<typename F>
	bool Iterate(F f)
	{
		bool interrupted = false;

		std::queue<std::pair<std::string, std::shared_ptr<Entry>>> q;
		q.push(std::pair<std::string, std::shared_ptr<Entry>>(std::string(""), RootDirectory()));

		while(!q.empty())
		{
			auto p = q.front();
			q.pop();

			if(p.second->IsDirectory())
				for(auto &dd : p.second->Entries())
					q.push(std::pair<std::string, std::shared_ptr<Entry>>(dd->IsDirectory() ? (p.first.empty() ? "" : p.first + "/") + dd->Name() : p.first, dd));
			else
			{
				if(f(p.first, p.second))
				{
					interrupted = true;
					break;
				}
			}
		}

		return interrupted;
	}

private:
	std::fstream _fsProxy;
	std::fstream &_fs;
	uint64_t _fileOffset;
	bool _scrambled;
	iso9660::VolumeDescriptor _pvd;
	uint32_t _trackOffset;
	uint32_t _trackSize;

	void Init();
};

}
