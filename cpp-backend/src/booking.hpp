/*
 * booking.hpp - Booking domain model and POSIX file-backed store.
 */
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <ctime>
#include <cstdint>

namespace app {

struct Booking {
    std::string id;        /* 24-hex-char id, MongoDB style */
    std::string name;
    std::string email;
    std::string datetime;  /* ISO-ish string as submitted */
    time_t      time = -1; /* parsed UTC seconds (-1 if unparseable) */
    int         people = 0;
    std::string message;
    time_t      createdAt = 0;

    /* Serialize to the JSON shape the admin/frontend expect
     * (includes "_id" for compatibility with the old Mongo API). */
    std::string toJson() const;
};

class BookingStore {
public:
    explicit BookingStore(std::string dataDir);

    /* Loads bookings.txt (creating the file if missing).
     * Returns false when the data directory cannot be prepared. */
    bool load();

    /*
     * Core business rule carried over from the Node backend:
     * max 10 seats per one-hour slot. Returns true and fills the
     * new booking when accepted; false and fills the slot state
     * otherwise (booked/seatsLeft for the UI message).
     */
    bool tryCreate(const std::string& name,
                   const std::string& email,
                   const std::string& datetime,
                   int people,
                   const std::string& message,
                   Booking& out,
                   int& bookedInSlot,
                   int& seatsLeft);

    /* Newest first (createdAt desc, id desc for stable ties). */
    std::vector<Booking> listAll() const;

    /* Remove by id; true when a row was deleted. */
    bool remove(const std::string& id);

    /* Sets *out when found. */
    bool find(const std::string& id, Booking* out) const;

    size_t count() const;

private:
    bool persistLocked() const;
    static std::string generateId();

    std::string dataDir_;
    mutable std::mutex mutex_;
    std::vector<Booking> bookings_; /* kept sorted by createdAt desc */
};

} // namespace app
