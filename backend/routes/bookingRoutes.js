const express = require("express");
const router = express.Router();
const Booking = require("../models/Booking");

// POST booking
router.post("/book", async (req, res) => {
    try {
        const { name, email, datetime, people, message } = req.body;

        const requestedTime = new Date(datetime);

        // create 1 hour slot range
        const start = new Date(requestedTime);
        start.setMinutes(0,0,0);

        const end = new Date(start);
        end.setHours(start.getHours() + 1);

        // find bookings in same hour
        const bookings = await Booking.find({
            datetime: { $gte: start, $lt: end }
        });

        // count total people already booked
        let totalPeople = 0;
        bookings.forEach(b => totalPeople += b.people);

        const MAX_CAPACITY = 10;

        if (totalPeople + Number(people) > MAX_CAPACITY) {
            return res.status(400).json({
                message: "This time slot is full. Please choose another time."
            });
        }

        // save booking
        const newBooking = new Booking({
            name,
            email,
            datetime: requestedTime,
            people,
            message
        });

        await newBooking.save();

        res.json({ message: "Booking Confirmed", booking: newBooking });

    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});


/* ---------- NEW GET ROUTE ---------- */

// get all bookings
router.get("/bookings", async (req, res) => {
    try {
        const bookings = await Booking.find().sort({ createdAt: -1 });
        res.json(bookings);
    } catch (error) {
        res.status(500).json({ message: "Error fetching bookings" });
    }
});

// DELETE booking
router.delete("/bookings/:id", async (req, res) => {
    try {
        await Booking.findByIdAndDelete(req.params.id);
        res.json({ message: "Booking deleted" });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

module.exports = router;