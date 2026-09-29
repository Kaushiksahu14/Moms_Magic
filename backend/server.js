const express = require("express");
const mongoose = require("mongoose");
const cors = require("cors");
require("dotenv").config();

const app = express();

app.use(cors());
app.use(express.json());

console.log("Starting server...");

// MongoDB connection
mongoose.connect(process.env.MONGO_URI)
.then(() => console.log("MongoDB Connected Successfully"))
.catch(err => console.log("MongoDB Error:", err));

/* ---------- ADD THIS LINE ---------- */
app.use("/api", require("./routes/bookingRoutes"));
/* ----------------------------------- */

// test route
app.get("/", (req, res) => {
    res.send("Backend is running");
});

// start server
app.listen(process.env.PORT, () => {
    console.log("Server running on port " + process.env.PORT);
});