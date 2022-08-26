#include "channel.hpp"
#include <array>
#include <future>
#include <iostream>
#include <chrono>

using array_type = std::array<char, 32 * 1024 * 1024>;
using payload_type = std::unique_ptr<array_type>;
using ordered_payload_type = std::pair<int, payload_type>;

// the reader reads 'count'x- payload-chunks from the pool and outputs them as enumerated ordered_payload through 'chan'
goroutine reader(std::shared_ptr<channel<ordered_payload_type>> chan,
                 std::shared_ptr<channel<payload_type>> pool, int count) 
{
  await_channel_writer<ordered_payload_type> writer{chan};
  await_channel_reader<payload_type> pool_reader{pool};

// enumerate the payload chunks in the order they were read from the pool
  for (int i = 0; i < count; ++i) 
  {
    auto res = co_await pool_reader.read();
	// Read data into res.second
    co_await writer.write({i, std::move(res.second)});
  }
  chan->close();
}

// the processor processes ordered_payload chunks one at a time 
goroutine processor (std::shared_ptr<channel<ordered_payload_type>> inchan,
                     std::shared_ptr<channel<ordered_payload_type>> outchan) 
{
	await_channel_reader<ordered_payload_type> reader{ inchan };
	await_channel_writer<ordered_payload_type> writer{ outchan };
	auto out_array = std::make_unique<array_type>();
	for (;;) {
		auto res = co_await reader.read(); // awaiter::await_resume() returns pair {'is channel still open?' , 'ordered_payload' }
		
    // if the read-channel was closed, close the output channel as well
    if (res.first == false) {
			outchan->close();
			co_return;
		}
		// Do something with res.second.second and out_array
		std::this_thread::sleep_for(std::chrono::seconds{ 1 });

		co_await writer.write({ res.second.first, std::move(out_array) }); 
    // 'res.second' was the ordered_payload, so 'res.second.first' is its order-index
		out_array = std::move(res.second.second);
	}
};

auto make_processor(std::shared_ptr<channel<ordered_payload_type>> inchan) {
  auto outchan = std::make_shared<channel<ordered_payload_type>>();
  processor(inchan, outchan);
   // goroutine::promise_type::initial_suspend()  suspends never -> processor is eagerly executed here
   // until it is suspended by a read on the 'inchan' (then the caller (make_processor is resumed ))
  return outchan;
}

// the writer reads what the 'threads'x processors have output through their ordered_payload-channels 
// and writes it back to the 'pool'
goroutine writer(std::vector<std::shared_ptr<channel<ordered_payload_type>>> inchans,
                 std::shared_ptr<channel<payload_type>> pool,
                 std::shared_ptr<channel<bool>> done_chan) 
{
  await_channel_writer<bool> done_writer{done_chan};
  std::vector<await_channel_reader<ordered_payload_type>> readers{inchans.begin(),
                                                       inchans.end()};

  await_channel_writer<payload_type> pool_writer{pool};

  std::vector<ordered_payload_type> buffer;
  auto min_heap_less =   [](auto &a, auto &b) { return a.first > b.first; };
  buffer.reserve(readers.size());
  int current = 0;
  for (;;) 
  {
    if (readers.empty() && buffer.empty()) {
      co_await done_writer.write(true);
      co_return;
    }
    auto res = co_await select_range(readers, [&](auto &i) {
      buffer.push_back(std::move(i)); // 'i' is an ordered_payload chunk
      std::push_heap(buffer.begin(), buffer.end(),              min_heap_less);
    });

    // select_range awaiter::await_resume returns pair {'selected_node still open?' , 'iterator to selected_node within range(readers)'}
    if (res.first == false) {
      readers.erase(res.second);// delete the corresponding await_channel_reader, if its node is closed
    }

    while (!buffer.empty() && buffer.front().first == current) {
	  // Write data in buffer.front().second to output
      std::cout << buffer.front().first << "\n";
      ++current;
      std::pop_heap(buffer.begin(), buffer.end(),
                  min_heap_less);
                    // buffer.back() is always the minimum element in the buffer 

      co_await pool_writer.write(std::move(buffer.back().second));
      buffer.pop_back();
    }
  }
}

int main() {
  int threads;  // how many processors shall divide the processing of the ordered_payload stream among themselfes
  int count; // how many ordered_payload chunks shall there be
  std::cout << "\nEnter count\n";
  std::cin >> count;
  std::cout << "\nEnter threads\n";
  std::cin >> threads;
  std::cout<< std::endl;

  std::vector<channel_runner> runners(threads);

  auto start = std::chrono::steady_clock::now();

  auto inchan = std::make_shared<channel<ordered_payload_type>>();
  auto pool = std::make_shared<channel<payload_type>>(threads); // make the channel's buffer size equal the thread count
  auto done_chan = std::make_shared<channel<bool>>();

 auto r =  reader(inchan, pool, count);

// start 'threads'x - processors that read from 'inchan' ( contest for writes to the channel )  (vying to get something off (writes) )
// and output (ordered_payloads ) each through their own 'outchan'
  std::vector<std::shared_ptr<channel<ordered_payload_type>>> ordered_payload_chans;
  for (int i = 0; i < threads; ++i) {
    ordered_payload_chans.push_back(make_processor(inchan));
  }

  writer(std::move(ordered_payload_chans), pool, done_chan);

  thread_suspender sus;
  sync_channel_reader<bool, thread_suspender> done_reader{done_chan, sus};
  sync_channel_writer<payload_type, thread_suspender> pool_writer{pool, sus};

  for (int i = 0; i < threads; ++i) {
    payload_type payload =
        std::make_unique<array_type>();
    pool_writer.write(std::move(payload));
  }

  done_reader.read();
  auto end = std::chrono::steady_clock::now();

  std::cout << "Took "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << " ms\n";
}