const mongoose = require("mongoose");

const bookingSchema = new mongoose.Schema({
    name: {
        type: String,
        required: true
    },
    email: {
        type: String,
        required: true
    },
    datetime: {
        type: String,
        required: true
    },
    people: {
        type: Number,
        required: true
    },
    message: {
        type: String
    },
    createdAt: {
        type: Date,
        default: Date.now
    }
});

module.exports = mongoose.model("Booking", bookingSchema);